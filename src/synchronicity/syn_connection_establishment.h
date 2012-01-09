#ifndef SYN_CONNECTION_ESTABLISHMENT_H_
#define SYN_CONNECTION_ESTABLISHMENT_H_

#include "synchronicity/syn_connection_internal.h"

// return sockfd or negative if failed
int syn_connection_host(SynConnectionInternal* const sci);
int syn_connection_connect(SynConnectionInternal* const sci);

int syn_connection_send_relay_server_key(uint64_t key, int sockfd);
int syn_connection_recv_relay_server_key(uint64_t* key, int sockfd);

// prototypes to shut up compiler
int syn_connection_helper_connect_to_relay_server(SynConnectionInternal* const sci);
int syn_connection_helper_relay_server_handshake(
    uint64_t* key,
    int sockfd);
#endif
