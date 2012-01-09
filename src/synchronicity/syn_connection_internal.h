#ifndef SYN_CONNECTION_INTERNAL_H_
#define SYN_CONNECTION_INTERNAL_H_

#include <synchronicity/syn_connection.h>
#include "synchronicity/syn_key_internal.h"

#define MAX_CONNECTIONS 10
#define SYN_THREAD_PRIORITY 10

#define SYN_CONNECTION_THRESHOLD 30000000
#define SYN_INTERNAL_IN_MICROS 500000
#define SYNC_MASK 0x1
#define SYNC_REPLY_MASK 0x2
#define SYNC_END 0x4
#define SYNC_ALPHA_INVERSE 4  // alpha is 1 / SYNC_ALPHA_INVERSE
#define SYNC_BETA_INVERSE 4
#define SYNC_INITIAL_COUNT 5

struct SynConnection_SendInfo {
  struct SynConnection_SendInfo* next;
  SynConnection_Callback* callback;
  void* param;
  uint32_t size;
  char* buffer;
};
typedef struct SynConnection_SendInfo SynConnection_SendInfo;

enum SynConnectionState {
  SYN_UNINITIALIZED = 0,
  SYN_SERVER_INITIALIZING,
  SYN_CLIENT_INITIALIZING,
  SYN_INITIALIZED,
  SYN_DESTROYING,
};
typedef enum SynConnectionState SynConnectionState;

struct Empty {
  VLC_COMMON_MEMBERS
};

struct SynConnectionInternal {
  vlc_mutex_t lock;  // protects this data structure

  SynConnectionState state;

  int socket;
  SynAddress address;

  vlc_thread_t send_thread;
  int receive_thread_initialized;
  vlc_thread_t receive_thread;

  const char* relay_server_host;
  int relay_server_port;

  // called when data is received
  SynConnection_ReceiveCallback* receive_callback;
  void* receive_param;

  // For client, this is called when it is connected to host
  // For host, this is called when it is ready to accept connection
  // ie the key is generated and ready to be displayed
  SynConnection_Callback* initialize_callback;
  void* initialize_param;

  SynConnection_Callback* heartbeat_callback;
  void* heartbeat_param;

  // For server only, called when peer is connected
  SynConnection_Callback* peer_connect_callback;
  void* peer_connect_param;
  int peer_connected;

  // Signaled when we have something to send
  vlc_cond_t send_info_non_empy;

  // link list of send buffers and callbacks
  SynConnection_SendInfo* send_info_head;
  SynConnection_SendInfo* send_info_tail;

  // called when socket is destroyed
  SynConnection_Callback* destroy_callback;
  void* destroy_param;

  int awaiting_reply;
  mtime_t await_start_time;
  mtime_t estimated_rtt;
  mtime_t estimated_rtt_stdev;
  int delta_t_initialized;
  mtime_t delta_t_confidence;
  mtime_t delta_t;

  struct Empty* useless_vlc_object;
};
typedef struct SynConnectionInternal SynConnectionInternal;

struct SynSegmentHeader {
  uint64_t flag;
  uint64_t timestamp_sync;
  uint64_t timestamp_reply;
  uint64_t length;
};
typedef struct SynSegmentHeader SynSegmentHeader;


// function prototypes
void SynLock(SynConnectionInternal* sci);
void SynUnlock(SynConnectionInternal* sci);
int csend_all(int socket, const void *buf, int len, struct Empty*);
int srecv_all(int socket, void *buf, int len, struct Empty*);

// prototypes to shut up compiler
void* syn_receive_thread(void* param);
void syn_set_sync_header(SynSegmentHeader* header);
void* syn_send_thread(void* param);
int SynConnection_InitializeHelper(
    vlc_object_t* parent,
    SynConnection* connection,
    const char* addr,  /* only used for client */
    const char* server_addr,
    int server_port,
    SynConnection_ReceiveCallback* receive_callback,
    void* receive_param,
    SynConnection_Callback* callback,
    void* param,
    SynConnection_Callback* peer_connect_callback,
    void* peer_connect_param,
    SynConnection_Callback* heartbeat_callback,
    void* heartbeat_param,
    int type);
void syn_connection_append_send_info(
    SynConnectionInternal* sci,
    SynConnection_Callback* callback,
    void* param,
    int len,
    void* buffer,
    uint64_t flag,
    uint64_t timestamp);

#endif
