// Meridian 59, Copyright 1994-2012 Andrew Kirmse and Chris Kirmse.
// All rights reserved.
//
// This software is distributed under a license that is described in
// the LICENSE file that accompanies it.
//
// Meridian is a registered trademark.
/*
 * async.c
 *
 */

#include "blakserv.h"

/* these are global because the windows async calls use them.  they
   should only be used in this file */

HANDLE name_lookup_handle;
client_msg udpbuf;
int last_udp_read_time = 0;

void AsyncEachSessionNameLookup(session_node *s);

#ifdef BLAK_PLATFORM_WINDOWS
void AcceptUDP(int socket_port)
{
   SOCKET udpsock;
   SOCKADDR_IN6 addr;

   memset(&addr, 0, sizeof(addr));
   addr.sin6_family = AF_INET6;
   addr.sin6_addr = in6addr_any;
   addr.sin6_flowinfo = 0;
   addr.sin6_scope_id = 0;
   addr.sin6_port = htons((short)socket_port);

   // try to create IPV6 UDP socket
   udpsock = socket(AF_INET6, SOCK_DGRAM, IPPROTO_UDP);
   if (udpsock == INVALID_SOCKET)
   {
      int error = WSAGetLastError();
      eprintf("AcceptUDP error creating udp socket - %i \n", error);
      return;
   }

   // set IPv6 DualStack to also support IPv4
   int yesno = 0;
   if (setsockopt(udpsock, IPPROTO_IPV6, IPV6_V6ONLY, (char *)&yesno, sizeof(yesno)) < 0)
   {
      int error = WSAGetLastError();
      eprintf("AcceptUDP error setting sock opts: IPV6_V6ONLY - %i \n", error);
      return;
   }

   // bind socket
   int rc = bind(udpsock, (SOCKADDR*)&addr, sizeof(addr));

   if (rc == SOCKET_ERROR)
   {
      int error = WSAGetLastError();
      eprintf("AcceptUDP error binding socket - %i \n", error);
      return;
   }

   StartAsyncSocketUDPRead(udpsock);
   /* when we get a udp datagram, it'll call AsyncSocketReadUDP */
}
#endif

void AsyncNameLookup(HANDLE hLookup,int error)
{
   if (error != 0)
   {
      /* eprintf("AsyncSocketNameLookup got error %i\n",error); */
      return;
   }
   
   name_lookup_handle = hLookup;
   
   EnterServerLock();
   ForEachSession(AsyncEachSessionNameLookup);
   LeaveServerLock();
   
}

void AsyncEachSessionNameLookup(session_node *s)
{
	if (s->conn.type != CONN_SOCKET)
		return;
	
	if (s->conn.hLookup == name_lookup_handle)
   {
      /*
      dprintf("AsyncEachSessionNameLookup on %s found match, address %i\n",
         s->conn.name,s->conn.hLookup);
         */
      PostThreadMessage(main_thread_id,WM_BLAK_MAIN_VERIFIED_LOGIN,
         s->session_id,0);
   }
}

void AsyncSocketRead(SOCKET sock)
{
   int bytes;
   session_node *s;
   buffer_node *bn;

   s = GetSessionBySocket(sock);
   if (s == NULL)
      return;

   if (s->hangup)
      return;

   if (!MutexAcquireWithTimeout(s->muxReceive,10000))
   {
      eprintf("AsyncSocketRead couldn't get session %i muxReceive",s->session_id);
      return;
   }

   if (s->receive_list == NULL)
   {
      s->receive_list = GetBuffer();
      /* dprintf("Read0x%08x\n",s->receive_list); */
   }

   // find the last buffer in the receive list
   bn = s->receive_list;
   while (bn->next != NULL)
      bn = bn->next;

   // if that buffer is filled to capacity already, get another and append it
   if (bn->len_buf >= BUFFER_SIZE_TCP)
   {
      bn->next = GetBuffer();
      /* dprintf("ReadM0x%08x\n",bn->next); */
      bn = bn->next;
   }

   // read from the socket, up to the remaining capacity of this buffer
   bytes = recv(s->conn.socket,bn->buf + bn->len_buf,BUFFER_SIZE_TCP - bn->len_buf,0);
   if (bytes == SOCKET_ERROR)
   {
      if (GetLastError() != WSAEWOULDBLOCK)
      {
         /* eprintf("AsyncSocketRead got read error %i\n",GetLastError()); */
         if (!MutexRelease(s->muxReceive))
            eprintf("File %s line %i release of non-owned mutex\n",__FILE__,__LINE__);
         HangupSession(s);
         return;
      }
      if (!MutexRelease(s->muxReceive))
         eprintf("File %s line %i release of non-owned mutex\n",__FILE__,__LINE__);
   }
   if (bytes == 0)
   {
      // read of 0 bytes means it's been closed; on windows we're
      // sent a specific close event instead
      if (!MutexRelease(s->muxReceive))
         eprintf("File %s line %i release of non-owned mutex\n",__FILE__,__LINE__);
      HangupSession(s);
      return;
   }


   if (bytes < 0 || bytes > BUFFER_SIZE_TCP - bn->len_buf)
   {
      eprintf("AsyncSocketRead got %i bytes from recv() when asked to stop at %i\n",bytes,BUFFER_SIZE_TCP - bn->len_buf);
      FlushDefaultChannels();
      bytes = 0;
   }

   bn->len_buf += bytes;

   if (!MutexRelease(s->muxReceive))
      eprintf("File %s line %i release of non-owned mutex\n",__FILE__,__LINE__);

   SignalSession(s->session_id);
}

void AsyncSocketWrite(SOCKET sock)
{
   int bytes;
   session_node *s;
   buffer_node *bn;

   s = GetSessionBySocket(sock);
   if (s == NULL)
      return;

   if (s->hangup)
      return;

   /* dprintf("got async write session %i\n",s->session_id); */
   if (!MutexAcquireWithTimeout(s->muxSend,10000))
   {
      eprintf("AsyncSocketWrite couldn't get session %i muxSend\n",s->session_id);
      return;
   }

   while (s->send_list != NULL)
   {
      bn = s->send_list;
      /* dprintf("async writing %i\n",bn->len_buf); */
      bytes = send(s->conn.socket,bn->buf,bn->len_buf,0);
      if (bytes == SOCKET_ERROR)
      {
         if (GetLastError() != WSAEWOULDBLOCK)
         {
            /* eprintf("AsyncSocketWrite got send error %i\n",GetLastError()); */
            if (!MutexRelease(s->muxSend))
               eprintf("File %s line %i release of non-owned mutex\n",__FILE__,__LINE__);
            HangupSession(s);
            return;
         }

         /* dprintf("got write event, but send would block\n"); */
         break;
      }
      else
      {
         if (bytes != bn->len_buf)
            dprintf("async write wrote %i/%i bytes\n",bytes,bn->len_buf);

         transmitted_bytes += bn->len_buf;

         s->send_list = bn->next;
         DeleteBuffer(bn);
      }
   }
   if (!MutexRelease(s->muxSend))
      eprintf("File %s line %i release of non-owned mutex\n",__FILE__,__LINE__);
}

void AsyncSocketClose(SOCKET sock)
{
   session_node *s;
   
   s = GetSessionBySocket(sock);
   if (s == NULL)
      return;
   
   lprintf("AsyncSocketClose found session %i hung up (socket %i)\n",
      s->session_id,sock);
   
   HangupSession(s);
}

#ifdef BLAK_PLATFORM_WINDOWS
void AsyncSocketReadUDP(SOCKET sock)
{
   /************* THIS IS EXECUTED BY THE UI/NETWORK THREAD *************/

   SOCKADDR_IN6 senderaddr;
   int          bytesReceivd = 0;
   int          flags = 0;
   int          lplen = sizeof(senderaddr);

   // Record time.
   last_udp_read_time = GetTime();

   ///////////////////////////////////////////////////////////////////////
   // try to receive the new udp datagram from socket
   ///////////////////////////////////////////////////////////////////////
   
   bytesReceivd = recvfrom(sock, (char*)&udpbuf, sizeof(udpbuf), flags,
      (SOCKADDR*)&senderaddr, &lplen);

   if (bytesReceivd == SOCKET_ERROR)
   {
      eprintf("AsyncSocketReadUDP error receiving UDP from socket - %i \n", 
         WSAGetLastError());
      return;
   }

   ///////////////////////////////////////////////////////////////////////
   // checks #1
   ///////////////////////////////////////////////////////////////////////

   // 1) not at least full header with byte, discard it
   if (bytesReceivd < SIZE_HEADER_UDP + 1)
   {
      eprintf("AsyncSocketReadUDP error udp with size below headersize \n");
      return;
   }

   // 2) todo?: check blacklisted ip by comparing senderaddr

   ///////////////////////////////////////////////////////////////////////
   // parse header and type
   ///////////////////////////////////////////////////////////////////////

   int sessionid      = *((int*)&udpbuf[0]);
   unsigned int seqno = *((unsigned int*)&udpbuf[4]);
   short crc          = *((short*)&udpbuf[8]);
   char epoch         = udpbuf[10];
   char type          = udpbuf[11];
   
   // try to get that session
   session_node* session = GetSessionByID(sessionid);

   ///////////////////////////////////////////////////////////////////////
   // checks #2
   ///////////////////////////////////////////////////////////////////////

   // Print errors/info?
   // make conditional without win32api behavior
   bool debug_udp = ConfigBool(DEBUG_UDP) != 0;

   // 1) invalid session or hangup
   if (!session || session->hangup)
   {
      if (debug_udp)
         dprintf("AsyncSocketReadUDP error unknown session-Id or hangup session \n");
      return;
   }

   // 2) important: udp sender ip must match tcp session ip to prevent attacks!
   for (unsigned int i = 0; i < 8; i++)
   {
      if (session->conn.addr.u.Word[i] != senderaddr.sin6_addr.u.Word[i])
      {
         if (debug_udp)
            eprintf("AsyncSocketReadUDP warning received session-Id from different IP\n");
         return;
      }
   }

   // 3) validate crc (calculated over type + data)
   short validcrc = (short)GetCRC16(udpbuf + SIZE_HEADER_UDP, bytesReceivd - SIZE_HEADER_UDP);
   if (crc != validcrc)
   {
      eprintf("AsyncSocketReadUDP error crc mismatch \n");
      return;
   }

   // 4) out of sequence order or duplicated UDP datagram
   if (seqno <= session->receive_seqno_udp)
   {
      if (debug_udp)
         dprintf("AsyncSocketReadUDP discarding out of sequence UDP on Session %i \n", session->session_id);
      return;
   }
   else
   {
      // Don't print, happens a bit too often and as a result of normal packet loss.
      // log missed or malformed UDP
      // if (seqno > session->receive_seqno_udp + 1)
      //    dprintf("AsyncSocketReadUDP detected lost or malformed UDP on Session %i", session->session_id);

      // update seqno
      session->receive_seqno_udp = seqno;
   }

   // 4) epoch: see GameProcessSessionBufferUDP()

   ///////////////////////////////////////////////////////////////////////
   // debug
   ///////////////////////////////////////////////////////////////////////

   //dprintf("Received valid UDP Session: %i Type: %i \n",
   //   sessionid, type);

   ///////////////////////////////////////////////////////////////////////
   // save data on session struct
   ///////////////////////////////////////////////////////////////////////

   // lock
   if (!MutexAcquireWithTimeout(session->muxReceive, 10000))
   {
      eprintf("AsyncSocketReadUDP couldn't get session %i muxReceive", 
         session->session_id);
      return;
   }

   // create first udp datagram buffer if none yet
   if (session->receive_list_udp == NULL)
      session->receive_list_udp = GetBuffer();

   // find the last buffer in the udp receive list
   buffer_node* bn = session->receive_list_udp;
   while (bn->next != NULL)
      bn = bn->next;

   // copy udp datagram into udp buffer list of session
   // note: this is simply 1 UDP datagram per buffer !
   bn->len_buf = bytesReceivd;
   memcpy(bn->prebuf, udpbuf, bytesReceivd);

   // unlock
   if (!MutexRelease(session->muxReceive))
      eprintf("File %s line %i release of non-owned mutex\n", __FILE__, __LINE__);

   ///////////////////////////////////////////////////////////////////////
   // signal mainthread (blakserv) to process data (thread transition here)
   ///////////////////////////////////////////////////////////////////////

   // wake up mainthread
   PostThreadMessage(main_thread_id, WM_BLAK_MAIN_READ, session->session_id, 0);
}
#endif
