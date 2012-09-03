#ifndef _RELAY_COMMON_H_
#define _RELAY_COMMON_H_

#include <errno.h>
#include <netdb.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

#include <arpa/inet.h>

namespace {
// To hide these functions from being used/linked against

// get sockaddr, IPv4 or IPv6:
void *get_in_addr(struct sockaddr *sa)
{
    if (sa->sa_family == AF_INET) {
        return &(((struct sockaddr_in*)sa)->sin_addr);
    }

    return &(((struct sockaddr_in6*)sa)->sin6_addr);
}

}

// Bind a socket to the port given and start listening for connections
// and return the socket
int prepare_server_socket(const char* port, const int backlog = 100) {
  int sockfd;
  struct addrinfo hints, *servinfo, *p;
  int rv;  // return value
  int yes=1;

  memset(&hints, 0, sizeof hints);
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_flags = AI_PASSIVE;

  // get server addrinfo to connect socket to
  if ((rv = getaddrinfo(NULL, port, &hints, &servinfo)) != 0) {
    fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
    exit(1);
  }

  // loop through all the results and bind to the first we can
  for(p = servinfo; p != NULL; p = p->ai_next) {
    if ((sockfd = socket(p->ai_family, p->ai_socktype,
          p->ai_protocol)) == -1) {
      perror("server: socket");
      continue;
    }

    if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &yes,
          sizeof(int)) == -1) {
      perror("setsockopt");
      exit(1);
    }

    if (bind(sockfd, p->ai_addr, p->ai_addrlen) == -1) {
      close(sockfd);
      perror("server: bind");
      continue;
    }

    break;
  }

  if (p == NULL)  {
    fprintf(stderr, "server: failed to bind\n");
    return -1;
  }

  freeaddrinfo(servinfo); // all done with this structure

  if (listen(sockfd, backlog) == -1) {
    perror("listen");
    exit(1);
  }

  return sockfd;
}

// Block and wait for accepting a connection on server socket
// Return the connecting socket
int accept_connection(int server_socket) {
  int new_socket;
  struct sockaddr_storage their_addr; // connector's address information
  socklen_t sin_size = sizeof their_addr;
  new_socket = accept(server_socket, (struct sockaddr *)&their_addr, &sin_size);

  if (new_socket == -1) {
    return new_socket;
  }

  // TODO(bo): Only compile this when debug flag is set
  char s[INET6_ADDRSTRLEN];
  inet_ntop(their_addr.ss_family,
      get_in_addr((struct sockaddr *)&their_addr),
      s, sizeof s);
  printf("server: got connection from %s\n", s);

  return new_socket;
}

// send with check. Returns number of bytes sent.
// This function does NOT guarantee that all bytes are sent
// TODO(bo): switch using csend_all where appropriate
int csend(int socket, const void *buf, int len) {
  int rv;
  if ((rv = send(socket, buf, len, 0)) == -1) {
    perror("send");
    exit(1);
  }
  return rv;
}

// safe send all. Never exists. Returns 0 only if everything went as expected
int ssend_all(int socket, const void *buf, int len) {
  int rv;
  while(len > 0) {
    rv = send(socket, buf, len, 0);
    if (rv == -1) {
      perror("send");
      return 1;
    }
    len -= rv;
    buf = (void*)((char*)buf + rv);
  }
  return 0;
}

// send all with check. Blocks until all of buffer is sent
void csend_all(int socket, const void *buf, int len) {
  int rv;
  while(len > 0) {
    rv = csend(socket, buf, len);
    len -= rv;
    buf = (void*)((char*)buf + rv);
  }
}

// recv with check. Returns number of bytes received
// This function does NOT guarantee that expected number of bytes are received.
// TODO(bo): switch to using crecv_all where appropriate
// TODO(bo): rename to crecv
int crecv_upto(int socket, void *buf, int len) {
  int rv;
  if((rv = recv(socket, buf, len, 0)) < 0) {
    perror("recv");
    exit(1);
  }
  return rv;
}

// safe recv_all, will never exit on error, will return 0 only
// if everything went as expected
int srecv_all(int socket, void *buf, int len) {
  int rv;
  while(len > 0) {
    rv = recv(socket, buf, len, 0);

    if(rv < 0) {
      perror("recv");
      return 1;
    }

    if (rv == 0) {
      fprintf(stderr, "connection closed while expecting data\n");
      return 2;
    }

    len -= rv;
    buf = (void*)((char*)buf + rv);
  }
  return 0;
}

// Block until received up to exactly len bytes
void crecv_all(int socket, void *buf, int len) {
  int rv;
  while(len > 0) {
    rv = crecv_upto(socket, buf, len);

    if (rv == 0) {
      fprintf(stderr, "connection closed while expecting data\n");
      exit(1);
    }

    len -= rv;
    buf = (void*)((char*)buf + rv);
  }
}

// Connect to address and port and return the socket.
// Meant to be used by client/tests to connect to server
int cconnect_to(const char* addr, const char* port) {
  int sockfd;
	struct addrinfo hints, *servinfo, *p;
  int rv;

	memset(&hints, 0, sizeof hints);
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;

	if ((rv = getaddrinfo(addr, port, &hints, &servinfo)) != 0) {
		fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
		return 1;
	}

	// loop through all the results and connect to the first we can
	for(p = servinfo; p != NULL; p = p->ai_next) {
		if ((sockfd = socket(p->ai_family, p->ai_socktype,
				p->ai_protocol)) == -1) {
			perror("client: socket");
      exit(1);
		}

		if (connect(sockfd, p->ai_addr, p->ai_addrlen) == -1) {
			close(sockfd);
			perror("client: connect");
      exit(1);
		}

		break;
	}

	if (p == NULL) {
		fprintf(stderr, "client: failed to connect\n");
		return 2;
	}

	char s[INET6_ADDRSTRLEN];
	inet_ntop(p->ai_family, get_in_addr((struct sockaddr *)p->ai_addr),
			s, sizeof s);
	printf("client: connecting to %s\n", s);

	freeaddrinfo(servinfo); // all done with this structure

  return sockfd;
}

// select with check. Given 2 sockets for read, block until
// one of them is ready to read, then return the socket that's ready.
// If both are ready to read, then return the first parameter.
int cselect_read_2(int sock1, int sock2) {
  fd_set readfds;
  int cap_fd;
  int rv;

  FD_ZERO(&readfds);
  FD_SET(sock1, &readfds);
  FD_SET(sock2, &readfds);

  if(sock1 < sock2) {
    cap_fd = sock2+1;
  } else {
    cap_fd = sock1+1;
  }

  // write fds, except fds, and time out to null
  rv = select(cap_fd, &readfds, NULL, NULL, NULL);
  if (rv < 0) {
    perror("select");
    exit(1);
  }

  if (rv == 0) {
    fprintf(stderr, "select should not have timed out");
    exit(1);
  }

  if(FD_ISSET(sock1, &readfds)) {
    return sock1;
  } else {
    return sock2;
  }
}
#endif  // _RELAY_COMMON_H_
