#ifndef SYN_CONNECTION_H_
#define SYN_CONNECTION_H_

#include <vlc_common.h>

#include <synchronicity/syn_key.h>

#define SYNC_STDEV_THRESHOLD 20000

struct SynConnection {
  int index;
};
typedef struct SynConnection SynConnection;

typedef void SynConnection_ReceiveCallback(
    int rv,
    mtime_t delay,
    void* param,
    void* buffer,
    size_t len);

VLC_API size_t SynConnection_GetAddrLen(
    SynConnection connection
);

VLC_API int SynConnection_GetAddr(
    SynConnection connection,
    char* out_buffer,
    size_t len
);

typedef void SynConnection_Callback(int rv, void* param);

VLC_API int SynConnection_InitializeAsServer(
    vlc_object_t* parent,
    SynConnection* connection,
    const char* server_addr,
    int server_port,
    SynConnection_ReceiveCallback* receive_callback,
    void* receive_param,
    SynConnection_Callback* host_key_callback,
    void* host_key_param,
    SynConnection_Callback* peer_connect_callback,
    void* peer_connect_param,
    SynConnection_Callback* heartbeat_callback,
    void* heartbeat_param
);
VLC_API int SynConnection_InitializeAsClient(
    vlc_object_t* parent,
    SynConnection* connection,
    const char* addr,
    const char* server_addr,
    int server_port,
    SynConnection_ReceiveCallback* receive_callback,
    void* receive_param,
    SynConnection_Callback* callback,
    void* param,
    SynConnection_Callback* heartbeat_callback,
    void* heartbeat_param
);
VLC_API int SynConnection_Destroy(
    SynConnection connection,
    SynConnection_Callback* callback,
    void* param
);
VLC_API int SynConnection_Send(
    SynConnection connection,
    void* buffer,
    size_t len,
    SynConnection_Callback* callback,
    void* param
);

#endif
