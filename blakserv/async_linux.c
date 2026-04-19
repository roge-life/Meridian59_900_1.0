// Meridian 59, Copyright 1994-2012 Andrew Kirmse and Chris Kirmse.
// All rights reserved.
//
// This software is distributed under a license that is described in
// the LICENSE file that accompanies it.
//
// Meridian is a registered trademark.

#include "blakserv.h"
#include <sys/epoll.h>

#define MAX_EPOLL_EVENTS 64
#define EPOLL_QUEUE_LEN 64

pthread_t network_thread;
pthread_t maintenance_thread;

#define MAX_MAINTENANCE_MASKS 15
char *maintenance_masks[MAX_MAINTENANCE_MASKS];
int num_maintenance_masks = 0;
char *maintenance_buffer = NULL;

int MakeNonBlockingSocket(int s);
void* NetworkWorker (void* arg);
void* UDPWorker (void* arg);
void ProcessSessionBuffer(session_node *s);
Bool CheckMaintenanceMask(SOCKADDR_IN6 *addr,int len_addr);

typedef struct {
   int socket;
   int connection_type;
} nWrkArgs;

void InitAsyncConnections(void)
{
    maintenance_buffer = (char *)malloc(strlen(ConfigStr(SOCKET_MAINTENANCE_MASK)) + 1);
    strcpy(maintenance_buffer,ConfigStr(SOCKET_MAINTENANCE_MASK));

    // now parse out each maintenance ip
    maintenance_masks[num_maintenance_masks] = strtok(maintenance_buffer,";");
    while (maintenance_masks[num_maintenance_masks] != NULL)
    {
        num_maintenance_masks++;
        if (num_maintenance_masks == MAX_MAINTENANCE_MASKS)
            break;
        maintenance_masks[num_maintenance_masks] = strtok(NULL,";");
    }
}

void ExitAsyncConnections(void)
{
}

void StartAsyncSocketAccept(SOCKET sock,int connection_type)
{
   int err;
   nWrkArgs* args = (nWrkArgs*) malloc(sizeof(nWrkArgs));

   args->socket = sock;
   args->connection_type = connection_type;

   if (connection_type == SOCKET_PORT)
   {
      err = pthread_create(&network_thread, NULL, &NetworkWorker, args);
   }
   else if (connection_type == SOCKET_MAINTENANCE_PORT)
   {
      err = pthread_create(&maintenance_thread, NULL, &NetworkWorker, args);
   }
   else
   {
      eprintf("Unable to start network worker thread! (Unknown port type)");
      free(args);
      return;
   }

   if (err != 0)
   {
      eprintf("Unable to start network worker thread! %s",strerror(err));
   }
}

void StartAsyncSocketUDPRead(SOCKET sock)
{
    pthread_t udp_thread;
    int* psock = (int*)malloc(sizeof(int));
    *psock = sock;

    if (pthread_create(&udp_thread, NULL, &UDPWorker, psock) != 0)
    {
        free(psock);
    }
}

HANDLE StartAsyncNameLookup(char *peer_addr,char *buf)
{
   return 0;
}

void StartAsyncSession(session_node *s)
{
}

void* NetworkWorker (void* _args)
{
   nWrkArgs* args = (nWrkArgs*) _args;
   int epoll_fd;
   int incoming_fd;
   int num_fds;
   struct epoll_event evt;
   struct epoll_event* events;

   epoll_fd = epoll_create(EPOLL_QUEUE_LEN);
   evt.events = EPOLLIN | EPOLLET;
   evt.data.fd = args->socket;

   events = (struct epoll_event*) calloc(MAX_EPOLL_EVENTS, sizeof(struct epoll_event));

   if (MakeNonBlockingSocket(args->socket) == -1)
   {
      eprintf("error in network worker thread! (make nonblock socket)");   
   } 

   epoll_ctl(epoll_fd, EPOLL_CTL_ADD, args->socket, &evt);

   while (true)
   {
      num_fds = epoll_wait(epoll_fd, events, MAX_EPOLL_EVENTS, -1);

      if (num_fds < 0)
      {
         if (errno == EINTR) continue;
         eprintf("error in network worker thread! (epoll_wait)!");
         break;
      }

      for(int i = 0; i < num_fds; i++)
      {
         if ((events[i].events & EPOLLERR) || (events[i].events & EPOLLHUP))
         {
            close(events[i].data.fd);
            continue;
         }
         
         if(events[i].data.fd == args->socket)
         {
            while (true)
            {
               incoming_fd = AsyncSocketAccept(events[i].data.fd, FD_ACCEPT, 0, args->connection_type);
               if (incoming_fd != SOCKET_ERROR && incoming_fd != 0)
               {
                   if (MakeNonBlockingSocket(incoming_fd) == -1)
                   {
                       eprintf("error in network worker thread! (make nonblock socket)");
                   }
                   
                   evt.data.fd = incoming_fd;
                   evt.events = EPOLLIN | EPOLLET; 
                   epoll_ctl(epoll_fd, EPOLL_CTL_ADD, incoming_fd, &evt);
               }
               else
               {
                  break;
               }
            }
         }
         else
         {
            EnterSessionLock();
            AsyncSocketRead(events[i].data.fd);
            
            // Dispatch logic specifically for maintenance
            if (args->connection_type == SOCKET_MAINTENANCE_PORT) {
               session_node *s = GetSessionBySocket(events[i].data.fd);
               if (s != NULL) ProcessSessionBuffer(s);
            }
            
            LeaveSessionLock();
         }
      }
   }

   free(events);
   free(args);
   return NULL;
}

void* UDPWorker(void* arg)
{
    int sock = *(int*)arg;
    free(arg);

    int epoll_fd;
    int num_fds;
    struct epoll_event evt;
    struct epoll_event* events;

    epoll_fd = epoll_create(EPOLL_QUEUE_LEN);
    evt.events = EPOLLIN | EPOLLET;
    evt.data.fd = sock;

    events = (struct epoll_event*) calloc(MAX_EPOLL_EVENTS, sizeof(struct epoll_event));

    if (MakeNonBlockingSocket(sock) == -1)
    {
        eprintf("error in UDP worker thread! (make nonblock socket)");
    }

    epoll_ctl(epoll_fd, EPOLL_CTL_ADD, sock, &evt);

    while (true)
    {
        num_fds = epoll_wait(epoll_fd, events, MAX_EPOLL_EVENTS, -1);
        if (num_fds < 0)
        {
            if (errno == EINTR) continue;
            eprintf("error in UDP worker thread! (epoll_wait)!");
            break;
        }

        for (int i = 0; i < num_fds; i++)
        {
            if (events[i].data.fd == sock)
            {
                EnterSessionLock();
                AsyncSocketReadUDP(sock);
                LeaveSessionLock();
            }
        }
    }

    free(events);
    return NULL;
}

int AsyncSocketAccept(SOCKET sock,int event,int error,int connection_type)
{
    SOCKET new_sock;
    SOCKADDR_IN6 acc_sin;
    socklen_t acc_sin_len;
    SOCKADDR_IN6 peer_info;
    socklen_t peer_len;
    struct in6_addr peer_addr;
    connection_node conn;
    session_node *s;

    if (event != FD_ACCEPT) return 0;
    if (error != 0) return 0;

    acc_sin_len = sizeof acc_sin;
    new_sock = accept(sock,(struct sockaddr *) &acc_sin,&acc_sin_len);

    if (new_sock == SOCKET_ERROR) return SOCKET_ERROR;

    peer_len = sizeof peer_info;
    if (getpeername(new_sock,(SOCKADDR *)&peer_info,&peer_len) < 0) return 0;

    memcpy(&peer_addr, &peer_info.sin6_addr, sizeof(struct in6_addr));
    memcpy(&conn.addr, &peer_addr, sizeof(struct in6_addr));
    inet_ntop(AF_INET6, &peer_addr, conn.name, sizeof(conn.name));

    if (connection_type == SOCKET_MAINTENANCE_PORT)
    {
        if (!CheckMaintenanceMask(&peer_info,peer_len))
        {
            closesocket(new_sock);
            return 0;
        }
    }

    conn.type = CONN_SOCKET;
    conn.socket = new_sock;

    EnterServerLock();
    s = CreateSession(conn);
    if (s != NULL)
    {
        switch (connection_type)
        {
        case SOCKET_PORT : InitSessionState(s,STATE_SYNCHED); break;
        case SOCKET_MAINTENANCE_PORT : InitSessionState(s,STATE_MAINTENANCE); break;
        }
        s->conn.hLookup = 0;
    }
    LeaveServerLock();

    return new_sock;
}

Bool CheckMaintenanceMask(SOCKADDR_IN6 *addr,int len_addr)
{
    IN6_ADDR mask;
    int i;
    for (i=0;i<num_maintenance_masks;i++)
    {
        if (maintenance_masks[i][0] == '*' && maintenance_masks[i][1] == '\0') return True;
        if (inet_pton(AF_INET6, maintenance_masks[i], &mask) != 1) continue;
        BOOL skip = False;
        for (int k = 0; k < 16; k++)
        {
            if (mask.u.Byte[k] != 0 && mask.u.Byte[k] != addr->sin6_addr.s6_addr[k])
            {
                skip = True;
                break;
            }
        }
        if (!skip) return True;
    }
    return False;
}

int MakeNonBlockingSocket(int s)
{
   int flags;
   if (s < 3) return -1;
   flags = fcntl(s, F_GETFL, 0);
   flags |= O_NONBLOCK;
   return fcntl(s, F_SETFL, flags);
}
