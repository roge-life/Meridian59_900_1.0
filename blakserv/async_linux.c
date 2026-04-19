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
pthread_t udp_thread;

// Global epoll FDs so StartAsyncSession can register new sockets
int game_epoll_fd = -1;
int maintenance_epoll_fd = -1;

#define MAX_MAINTENANCE_MASKS 15
char *maintenance_masks[MAX_MAINTENANCE_MASKS];
int num_maintenance_masks = 0;
char *maintenance_buffer = NULL;

int MakeNonBlockingSocket(int s);
void* NetworkWorker (void* arg);
void* UDPWorker (void* arg);

typedef struct {
   int socket;
   int connection_type;
} nWrkArgs;

Bool CheckMaintenanceMask(SOCKADDR_IN6 *addr,int len_addr);

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

    // Initialize global epoll descriptors
    game_epoll_fd = epoll_create(EPOLL_QUEUE_LEN);
    maintenance_epoll_fd = epoll_create(EPOLL_QUEUE_LEN);
}

void ExitAsyncConnections(void)
{
    if (game_epoll_fd != -1) close(game_epoll_fd);
    if (maintenance_epoll_fd != -1) close(maintenance_epoll_fd);
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
      free(args);
   }
   else
   {
      dprintf("network worker thread started for connection type %d on fd %d",connection_type,sock);
   }
}

void StartAsyncSocketUDPRead(SOCKET sock)
{
    int err;
    int* psock = (int*)malloc(sizeof(int));
    *psock = sock;

    err = pthread_create(&udp_thread, NULL, &UDPWorker, psock);
    if (err != 0)
    {
        eprintf("Unable to start UDP worker thread! %s", strerror(err));
        free(psock);
    }
    else
    {
        dprintf("UDP worker thread started on fd %d", sock);
    }
}

HANDLE StartAsyncNameLookup(char *peer_addr,char *buf)
{
   // TODO: stub
   return 0;
}

void StartAsyncSession(session_node *s)
{
    struct epoll_event evt;
    int target_epoll = -1;

    if (s->state == STATE_SYNCHED) {
        target_epoll = game_epoll_fd;
    } else if (s->state == STATE_MAINTENANCE || s->state == STATE_ADMIN) {
        target_epoll = maintenance_epoll_fd;
    }

    if (target_epoll != -1) {
        if (MakeNonBlockingSocket(s->conn.socket) == -1) {
            eprintf("StartAsyncSession: error making socket non-blocking for fd %d", s->conn.socket);
        }
        evt.data.fd = s->conn.socket;
        evt.events = EPOLLIN | EPOLLET;
        if (epoll_ctl(target_epoll, EPOLL_CTL_ADD, s->conn.socket, &evt) == -1) {
            eprintf("StartAsyncSession: epoll_ctl ADD failed for fd %d, error %d", s->conn.socket, errno);
        }
    }
}

void* NetworkWorker (void* _args)
{
   nWrkArgs* args = (nWrkArgs*) _args;
   int epoll_fd;
   int incoming_fd;
   int num_fds;
   struct epoll_event evt;
   struct epoll_event* events;

   // Select the correct global epoll FD for this worker
   if (args->connection_type == SOCKET_PORT) {
       epoll_fd = game_epoll_fd;
   } else {
       epoll_fd = maintenance_epoll_fd;
   }

   evt.events = EPOLLIN | EPOLLET;
   evt.data.fd = args->socket;

   events = (epoll_event*) calloc(MAX_EPOLL_EVENTS, sizeof(struct epoll_event));

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
         if ((events[i].events & EPOLLERR) ||
              (events[i].events & EPOLLHUP))
         {
            eprintf ("error in network worker thread! (socket error)");
            close (events[i].data.fd);
            continue;
         }
         
         if(events[i].data.fd == args->socket)
         {
            while (true)
            {
               incoming_fd = AsyncSocketAccept(events[i].data.fd, FD_ACCEPT, 0, args->connection_type);
               if (incoming_fd == SOCKET_ERROR || incoming_fd == 0)
                  break;
            }
         }
         else
         {
            EnterSessionLock();
            AsyncSocketRead(events[i].data.fd);
            
            // Dispatch the buffer to the state-specific handler
            s = GetSessionBySocket(events[i].data.fd);
            if (s != NULL)
            {
               ProcessSessionBuffer(s);
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

    events = (epoll_event*) calloc(MAX_EPOLL_EVENTS, sizeof(struct epoll_event));

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
                // For UDP, we keep reading while data is available because of EPOLLET
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

    if (new_sock == SOCKET_ERROR)
    {
        if (errno != EAGAIN && errno != EWOULDBLOCK)
        {
            eprintf("AcceptSocketConnections accept failed, error %i\n", GetLastError());
        }
        return SOCKET_ERROR;
    }

    peer_len = sizeof peer_info;
    if (getpeername(new_sock,(SOCKADDR *)&peer_info,&peer_len) < 0)
    {
        eprintf("AcceptSocketConnections getpeername failed error %i\n", GetLastError());
        return 0;
    }

    memcpy(&peer_addr, &peer_info.sin6_addr, sizeof(struct in6_addr));
    memcpy(&conn.addr, &peer_addr, sizeof(struct in6_addr));
    inet_ntop(AF_INET6, &peer_addr, conn.name, sizeof(conn.name));

    if (connection_type == SOCKET_MAINTENANCE_PORT)
    {
        if (!CheckMaintenanceMask(&peer_info,peer_len))
        {
            lprintf("Blocked maintenance connection from %s.\n", conn.name);
            closesocket(new_sock);
            return 0;
        }
    }
    else
    {
        if (!CheckBlockList(&peer_addr))
        {
            lprintf("Blocked connection from %s.\n", conn.name);
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
        // Set state BEFORE starting async session so StartAsyncSession knows which epoll to use
        switch (connection_type)
        {
        case SOCKET_PORT : InitSessionState(s,STATE_SYNCHED); break;
        case SOCKET_MAINTENANCE_PORT : InitSessionState(s,STATE_MAINTENANCE); break;
        }
        
        StartAsyncSession(s);
        s->conn.hLookup = 0;
    }
    LeaveServerLock();

    return new_sock;
}

Bool CheckMaintenanceMask(SOCKADDR_IN6 *addr,int len_addr)
{
    struct in6_addr mask;
    int i;
    BOOL skip;

    for (i=0;i<num_maintenance_masks;i++)
    {
        if (inet_pton(AF_INET6, maintenance_masks[i], &mask) != 1)
        {
            eprintf("CheckMaintenanceMask has invalid configured mask %s\n", maintenance_masks[i]);
            continue;
        }

        skip = 0;
        for (int k = 0; k < 16; k++)
        {
            if (mask.s6_addr[k] != 0 && mask.s6_addr[k] != addr->sin6_addr.s6_addr[k])
            {
                skip = 1;
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
