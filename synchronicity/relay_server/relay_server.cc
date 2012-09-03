#include <arpa/inet.h>
#include <errno.h>
#include <netdb.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <sstream>

#include <map>
#include <set>

#include "relay_common.h"
#include "connection_map.h"

#define STARTING_PORT 56789
#define MIN_PORT 1025
#define MAX_PORT 65535

struct PendingSocket {
  int sockfd;

  PendingSocket(int sockfd)
    : sockfd(sockfd) {
  }
};


typedef std::map<ConnectionMapKey,
         PendingSocket> ConnectionMap;
ConnectionMap connection_map;

typedef int SocketDescriptor;
typedef std::set<SocketDescriptor> SocketSet;

bool running;
int server_sockfd;  // listen on sock_fd, new connection on new_fd

void sigchld_handler(int s)
{
    while(waitpid(-1, NULL, WNOHANG) > 0);
    printf("child died");
}

void sigint_handler(int s) {
  running = false;
  close(server_sockfd);
}

// This is the only function executed by child process
void forked_child_worker(int sock1, int sock2) {
  int ready_sock = sock2;
  int other_sock;
  char buffer[10240];

  while(running) {
    if(ready_sock == sock1) {
      ready_sock = cselect_read_2(sock2, sock1);
    } else {
      ready_sock = cselect_read_2(sock1, sock2);
    }

    if(ready_sock == sock1) {
      other_sock = sock2;
    } else {
      other_sock = sock1;
    }

    int len = crecv_upto(ready_sock, buffer, 10240);

    if(len == 0) {
      printf("connection closing\n");
      close(sock1);
      close(sock2);
      break;
    }

    int rv = ssend_all(other_sock, buffer, len);
    if (rv != 0) {
      printf("Error: Could not relay message.\n");
      continue;
    }
  }

  exit(0);
}

void ConnectionPairReady(int sockfd1, int sockfd2) {
  if(!fork()) {
    // Child process
    close(server_sockfd);
    connection_map.clear();
    forked_child_worker(sockfd1, sockfd2); // never returns
  } else {
    // Parent
    close(sockfd1);
    close(sockfd2);
  }
}

int SetupPendingSockets(SocketSet& pending_sockets, fd_set& sockets) {
  int max_fd = -1;
  FD_ZERO(&sockets);
  for(SocketSet::iterator it = pending_sockets.begin();
      it != pending_sockets.end();
      it++) {
    FD_SET(*it, &sockets);
    if (*it > max_fd) {
      max_fd = *it;
    }
  }
  return max_fd;
}

SocketDescriptor GetReadableSocket(SocketSet& pending_sockets, fd_set& sockets) {
  for(SocketSet::iterator it = pending_sockets.begin();
      it != pending_sockets.end();
      it++) {
      if(FD_ISSET(*it, &sockets)) {
        return *it;
      }
  }
  return -1;
}

int main(void) {
  struct sigaction sa;

  int port = STARTING_PORT;
  char port_buffer[7];
  while (true) {
    sprintf(port_buffer, "%d", port);
    server_sockfd = prepare_server_socket(port_buffer);
    if (server_sockfd != -1 ) {
      printf("PORT: %d\n", port);
      FILE *portf = fopen("port", "w");
      fprintf(portf, "PORT: %d\n", port);
      fclose(portf);
      break;
    }
    else {
      printf("PORT: %d failed, incrementing.\n", port);
    }
    port++;
    if (port == MAX_PORT) port = MIN_PORT;
  }

  sa.sa_handler = sigchld_handler; // reap all dead processes
  sigemptyset(&sa.sa_mask);
  sa.sa_flags = SA_RESTART;
  if (sigaction(SIGCHLD, &sa, NULL) == -1) {
    perror("sigaction");
    exit(1);
  }
  sa.sa_handler = sigint_handler; // Handle ctrl+c
  sigemptyset(&sa.sa_mask);
  if (sigaction(SIGINT, &sa, NULL) == -1) {
    perror("sigaction");
    exit(1);
  }

  printf("server: waiting for connections...\n");

  running = true;

  fd_set sockets;
  SocketSet readable_sockets;
  readable_sockets.insert(server_sockfd);

  ConnectionMapKeyBuffer key_buffer;
  int rv;
  struct timeval timeout;
  while(running) {
    int max_fd = SetupPendingSockets(readable_sockets, sockets);
    timeout.tv_sec = 15*60;
    timeout.tv_usec = 0;
    int num_ready = select(max_fd+1,
        &sockets,       //Readable set
        (fd_set*) NULL, //Writable set
        (fd_set*) NULL, //Exception set
        &timeout);      //Timeout
    if (-1 == num_ready) {
      perror("Select");
      continue;
    } else if (0 == num_ready) {
      // Timeout
      for(SocketSet::iterator iter = readable_sockets.begin();
          readable_sockets.end() != iter;
          ++iter) {
        if (server_sockfd != *iter) {
          close(*iter);
          printf("Closed pending connection\n");
        }
      }
      readable_sockets.clear();
      readable_sockets.insert(server_sockfd);

      for(ConnectionMap::iterator iter = connection_map.begin();
          connection_map.end() != iter;
          ++iter) {
        close((*iter).second.sockfd);
        printf("Deleting key %#llx\n", (*iter).first);
      }

      connection_map.clear();
      continue;
    }
    int socket = GetReadableSocket(readable_sockets, sockets);
    if (server_sockfd == socket) {
      int client_socket = accept_connection(server_sockfd);
      if(-1 == client_socket) {
        perror("accept");
        break;
      }
      if (!running) {
        break;
      }
      readable_sockets.insert(client_socket);
    } else {
      readable_sockets.erase(socket);

      ConnectionMapKey key;
      rv = srecv_all(socket, key_buffer, KEY_BUFFER_LENGTH);
      if(0 != rv) {
        close(socket);
        printf("Error: did not receive key\n");
        continue;
      }

      rv = ParseKey(key_buffer, key);
      if(0 != rv) {
        close(socket);
        printf("Error: Cannot parse key\n");
        continue;
      }

      if(0 == key) {
        do {
          key = RandomKey();
        } while(0 == key ||
            connection_map.find(key) != connection_map.end());

        PrintKey(key, key_buffer);
        rv = ssend_all(socket, key_buffer, KEY_BUFFER_LENGTH);
        if(0 != rv) {
          close(socket);
          printf("Error: Cannot send key\n");
          continue;
        }
        connection_map.insert(ConnectionMap::value_type(
              key, PendingSocket(socket)));
        printf("Inserting with key %#llx\n", key);
      } else {
        ConnectionMap::iterator itr = connection_map.find(key);
        if(itr == connection_map.end()) {
          close(socket);
          printf("Error: Cannot find matching socket with key %#llx\n", key);
          continue;
        }
        int first_socket = itr->second.sockfd;
        connection_map.erase(itr);
        printf("Matched with key %#llx\n", itr->first);

        key = 0;
        PrintKey(key, key_buffer);
        rv = ssend_all(socket, key_buffer, KEY_BUFFER_LENGTH);
        if(0 != rv) {
          close(socket);
          printf("Error: Cannot send 0 key\n");
          continue;
        }

        ConnectionPairReady(first_socket, socket);
      }
    }
  }

  printf("Exiting...\n");

  return 0;
}
