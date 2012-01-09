#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <netdb.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <sys/socket.h>

#include <arpa/inet.h>

#include "connection_map.h"
#include "relay_common.h"

#define STDIN 0
#define STDOUT 1

int main(int argc, char* argv[]) {
  int sockfd = cconnect_to(argv[1],
                           "6789");
  ConnectionMapKeyBuffer receivedKey;
  read(STDIN, receivedKey, KEY_LENGTH);
  csend(sockfd, receivedKey, KEY_LENGTH);
  //the next few lines set up and print the key used to connect with this client
  //crecv_upto(sockfd, receivedKey, KEY_LENGTH);
  //write(STDOUT, receivedKey, KEY_LENGTH);
  
  int rv;
  char buffer[10240];
  while(true) {
    int sock = cselect_read_2(STDIN, sockfd);
    if(sock == STDIN) {
      rv = read(STDIN, buffer, 10240);
      if(rv == 0) {
        break;
      }
      csend(sockfd, buffer, rv);
    } else {
      rv = crecv_upto(sockfd, buffer, 10240);
      write(STDOUT, buffer, rv);
    }
  }
  close(sockfd);
  return 0;
}
