#include "synchronicity/syn_connection_establishment.h"
#include <vlc_network.h>

#include "synchronicity/syn_key_internal.h"

int syn_connection_helper_connect_to_relay_server(SynConnectionInternal* const sci) {
  return net_ConnectTCP(sci->useless_vlc_object,
      sci->relay_server_host,
      sci->relay_server_port);
}

int syn_connection_send_relay_server_key(uint64_t key, int sockfd) {
  char key_buffer[SYN_KEY_BUFFER_LENGTH+1];
  uint64_to_char(key, key_buffer);
  return csend_all(sockfd, &key_buffer, SYN_KEY_BUFFER_LENGTH, 0);
}

int syn_connection_recv_relay_server_key(uint64_t* key, int sockfd) {
  char key_buffer[SYN_KEY_BUFFER_LENGTH+1];
  int rv = srecv_all(sockfd, &key_buffer, SYN_KEY_BUFFER_LENGTH, 0);
  if(rv <= 0) {
    return -100 + rv;
  }
  return char_to_uint64(key, key_buffer);
}

int syn_connection_helper_relay_server_handshake(
    uint64_t* key,
    int sockfd) {
  // Send hankshake key
  int rv = syn_connection_send_relay_server_key(*key, sockfd);
  if(rv < 0) {
    return -200 + rv;
  }

  // Receive hankshake key
  rv = syn_connection_recv_relay_server_key(key, sockfd);
  if(rv < 0) {
    return -300 + rv;
  } else {
    return 0;
  }
}

int syn_connection_host(SynConnectionInternal* const sci) {
  int sockfd = syn_connection_helper_connect_to_relay_server(sci);
  if(sockfd < 0) {
    return sockfd;
  }

  // variables for handshake
  uint64_t key = 0;
  int rv = syn_connection_helper_relay_server_handshake(&key, sockfd);
  if(rv < 0) {
    return rv;
  }

  // Set socket
  SynLock(sci);
    if(SYN_DESTROYING == sci->state) return -1000;
    sci->socket = sockfd;
    sci->address.relay_server_key = key;
  SynUnlock(sci);

  return 0;
}

int syn_connection_connect(SynConnectionInternal* const sci) {
  int sockfd = syn_connection_helper_connect_to_relay_server(sci);
  if(sockfd < 0) {
    return sockfd;
  }

  // get key to send to relay server
  SynLock(sci);
    if(SYN_DESTROYING == sci->state) return -1000;
    uint64_t key = sci->address.relay_server_key;
  SynUnlock(sci);

  // handshake with relay server
  int rv = syn_connection_helper_relay_server_handshake(&key, sockfd);
  if(rv < 0) {
    return rv;
  }

  // check key is 0
  if(0 != key) {
    return -500;
  }

  // Set socket
  SynLock(sci);
    if(SYN_DESTROYING == sci->state) return -1000;
    sci->socket = sockfd;
  SynUnlock(sci);

  return 0;
}
