#include <synchronicity/syn_connection.h>
#include "synchronicity/syn_connection_internal.h"
#include "synchronicity/syn_key_internal.h"
#include "synchronicity/syn_connection_establishment.h"

#include <vlc_threads.h>
#include <vlc_network.h>
#include <vlc_rand.h>
#include <assert.h>
#include "libvlc.h"

static SynConnectionInternal syn_ci_[MAX_CONNECTIONS];

void SynLock(SynConnectionInternal* sci) {
  vlc_mutex_lock(&sci->lock);
}

void SynUnlock(SynConnectionInternal* sci) {
  vlc_mutex_unlock(&sci->lock);
}

// send all with check. Blocks until all of buffer is sent
int csend_all(int socket, const void *buf, int len,
    struct Empty* useless_vlc_object) {
  int i = 0;
  while(!useless_vlc_object) {
    useless_vlc_object = syn_ci_[i++].useless_vlc_object;
  }
  return net_Write(useless_vlc_object, socket, 0, buf, len);
}


// safe recv_all, will never exit on error, will return 0 only
// if everything went as expected
int srecv_all(int socket, void *buf, int len,
    struct Empty* useless_vlc_object) {
  int i = 0;
  while(!useless_vlc_object) {
    useless_vlc_object = syn_ci_[i++].useless_vlc_object;
  }
  int rv = net_Read(useless_vlc_object, socket, 0, buf, len, 1);
  if(rv < 0) {
    return rv;
  } else if(rv < len) {
    return 0;
  } else {
    return len;
  }
}

void* syn_receive_thread(void* param) {
  int index = (int)param;
  SynConnectionInternal* sci = &syn_ci_[index];
  int sockfd = sci->socket;
  SynConnection_ReceiveCallback* callback = sci->receive_callback;
  param = sci->receive_param;

  unsigned int count_sync_reply = 0;
  static const unsigned int RECV_BUFFER_SIZE = 100;
  char buffer[RECV_BUFFER_SIZE];
  SynSegmentHeader header;
  while(true) {
    SynLock(sci);
      if(SYN_DESTROYING == sci->state) {
        return NULL;
      }
    SynUnlock(sci);

    // receive header
    int rv = srecv_all(sockfd, &header, sizeof(header),
        sci->useless_vlc_object);

    // recv data is there is any to receive
    if(rv > 0) {
      if(!sci->peer_connected) {
        SynLock(sci);
        sci->peer_connected = 1;
        vlc_cond_signal(&sci->send_info_non_empy);
        SynUnlock(sci);
      }
      if(sci->peer_connect_callback &&
          (count_sync_reply >= SYNC_INITIAL_COUNT)) {
        // TODO this block of code is duplicated
        SynLock(sci);
        SynConnection_Callback* peer_connect_callback =
          sci->peer_connect_callback;
        void* peer_connect_param = sci->peer_connect_param;
        sci->peer_connect_callback = 0;
        sci->peer_connect_param = 0;
        SynUnlock(sci);

        (*peer_connect_callback)(0, peer_connect_param);
      }

      if(header.flag & SYNC_MASK) {
        int mask = SYNC_REPLY_MASK;
        if(!(header.flag & SYNC_END)) {
          mask |= SYNC_MASK | SYNC_END;
        }

        // send with SYNC_REPLY_MASK
        syn_connection_append_send_info(
            sci, 0, 0, 0, 0,
            mask, header.timestamp_sync);
      }

      mtime_t current_mdate = mdate();
      if(header.flag & SYNC_REPLY_MASK) {
        count_sync_reply++;
        sci->awaiting_reply = 0;
        if(count_sync_reply < SYNC_INITIAL_COUNT) {
          vlc_cond_signal(&sci->send_info_non_empy);
        }
        // update rtt
        mtime_t sample_rtt = current_mdate - header.timestamp_reply;
        SynLock(sci);
          if(sci->estimated_rtt < 0) {
            sci->estimated_rtt = sample_rtt;
            sci->estimated_rtt_stdev = sample_rtt;
          } else {
            mtime_t samp_diff = sci->estimated_rtt - sample_rtt;
            if(samp_diff < 0) {
              samp_diff = -samp_diff;
            }
            sci->estimated_rtt_stdev = ((SYNC_BETA_INVERSE - 1) * sci->estimated_rtt_stdev +
                samp_diff) / SYNC_BETA_INVERSE;
            sci->estimated_rtt = ((SYNC_ALPHA_INVERSE - 1) * sci->estimated_rtt +
                sample_rtt) / SYNC_ALPHA_INVERSE;
          }
          mtime_t new_delta_t = current_mdate - sample_rtt / 2 - header.timestamp_sync;
          if(sci->delta_t_initialized) {
            // complicated math follows...doing a weighted sum of new_delta_t and old delta_t
            sci->delta_t = (new_delta_t * sci->delta_t_confidence + sci->delta_t * sample_rtt) /
              (sci->delta_t_confidence + sample_rtt);
            sci->delta_t_confidence = (sample_rtt * sci->delta_t_confidence + sample_rtt * sci->delta_t_confidence) /
              (sci->delta_t_confidence + sample_rtt);
          } else {
            sci->delta_t = new_delta_t;
            sci->delta_t_confidence = sample_rtt;
            sci->delta_t_initialized = 1;
          }
          if(sci->peer_connect_callback &&
              (count_sync_reply >= SYNC_INITIAL_COUNT)) {
            // TODO this block of code is duplicated
            SynConnection_Callback* peer_connect_callback =
              sci->peer_connect_callback;
            void* peer_connect_param = sci->peer_connect_param;
            sci->peer_connect_callback = 0;
            sci->peer_connect_param = 0;
            SynUnlock(sci);

            (*peer_connect_callback)(0, peer_connect_param);

            SynLock(sci);
          }
        SynUnlock(sci);
      }

      mtime_t delay = current_mdate - header.timestamp_sync - sci->delta_t;

      if(header.length > 0) {
        // receive data
        if(header.length > RECV_BUFFER_SIZE) {
          char* buffer_on_heap = malloc(header.length);
          rv = srecv_all(sockfd, buffer_on_heap, header.length,
              sci->useless_vlc_object);
          (*callback)(rv, delay, param, buffer_on_heap, header.length);
          free(buffer_on_heap);
        } else {
          rv = srecv_all(sockfd, buffer, header.length,
              sci->useless_vlc_object);
          (*callback)(rv, delay, param, buffer, header.length);
        }
      }
    } else {
      if(SYN_DESTROYING != sci->state) {
        (*callback)(rv, sci->estimated_rtt / 2, param, 0, 0);
      }
    }

    if(rv <= 0) {
      break;
    }
  }

  return 0;
}

void syn_set_sync_header(SynSegmentHeader* header) {
  header->timestamp_sync = mdate();
}

void* syn_send_thread(void* param) {
  int index = (int)param;
  SynConnectionInternal* const sci = &syn_ci_[index];

  int rv = -1;
  if(SYN_SERVER_INITIALIZING == sci->state) {
    rv = syn_connection_host(sci);
  } else if(SYN_CLIENT_INITIALIZING == sci->state) {
    rv = syn_connection_connect(sci);
  }
  if(rv < 0) {
    goto SynDestroyWithIniailize;
  }

  // Call initialize callback for finished hosting/connecting
  SynLock(sci);
    if(SYN_DESTROYING == sci->state) goto SynDestroying;

    // update state and remove initialize callback
    SynConnection_Callback* initialize_callback =
      sci->initialize_callback;
    void* init_param = sci->initialize_param;
    sci->initialize_callback = NULL;
    sci->initialize_param = NULL;
  SynUnlock(sci);
  if(initialize_callback) {
    (*initialize_callback)(0, init_param);
  }

  int was_client = 0;
  SynLock(sci);
    if(SYN_CLIENT_INITIALIZING == sci->state) {
      //sci->peer_connected = 1;
      was_client = 1;
    }
    sci->state = SYN_INITIALIZED;
  SynUnlock(sci);

  // Start receive thread
  rv = vlc_clone(
      &sci->receive_thread,
      &syn_receive_thread,
      (void*)index,
      SYN_THREAD_PRIORITY);
  if(0 != rv) {
    msg_Err(sci->useless_vlc_object, "recv thread failed to start");
    rv = -4;
    goto SynDestroyWithIniailize;
  }
  SynLock(sci);
    sci->receive_thread_initialized = 1;
  SynUnlock(sci);

  unsigned int count_sync = 0;
  while(true) {
    SynLock(sci);
      if(SYN_DESTROYING == sci->state) goto SynDestroying;

      if(NULL == sci->send_info_head) {
        mtime_t delay;
        if(count_sync < SYNC_INITIAL_COUNT && sci->peer_connected &&
            !sci->awaiting_reply) {
          delay = 0;
        } else {
          delay = sci->estimated_rtt + 2 * sci->estimated_rtt_stdev;
          if(delay < SYN_INTERNAL_IN_MICROS) {
            delay = SYN_INTERNAL_IN_MICROS;
          }
          delay = delay * (vlc_lrand48() % 10 + 1);
          vlc_cond_timedwait(&sci->send_info_non_empy, &sci->lock,
              mdate() + delay);
        }
      }
      SynConnection_SendInfo* send_info = NULL;
      if(NULL != sci->send_info_head) {
        send_info  = sci->send_info_head;
        sci->send_info_head = send_info->next;
        if(NULL == send_info->next) {
          sci->send_info_tail = NULL;
        }
      }
    SynUnlock(sci);

    if(sci->awaiting_reply &&
      (mdate() - sci->await_start_time) > SYN_CONNECTION_THRESHOLD) {
      goto SynDestroyWithIniailize;
    }

    if(NULL != send_info) {
      // send data
      SynSegmentHeader* header = (SynSegmentHeader*)(send_info->buffer);
      syn_set_sync_header(header);
      if(header->flag & SYNC_MASK) {
        if(sci->awaiting_reply) {
          header->flag &= ~SYNC_MASK;
        } else {
          sci->awaiting_reply = 1;
          sci->await_start_time = mdate();
          count_sync++;
        }
      }

      if(rv >= 0) {
        rv = csend_all(sci->socket, send_info->buffer, send_info->size,
            sci->useless_vlc_object);
      }

      // Execute callback
      if(NULL != send_info->callback) {
        (*send_info->callback)(rv < 0 ? rv : 0, send_info->param);
      }

      // delete resources
      free(send_info->buffer);
      free(send_info);

      if(rv < 0) {
        msg_Err(sci->useless_vlc_object, "send thread error %d %m", rv);
        rv = -5;
        goto SynDestroyWithIniailize;
      }
    } else if((sci->peer_connected && !sci->awaiting_reply) ||
        (was_client && 0 == count_sync)) {
      SynSegmentHeader header;
      header.flag = SYNC_MASK;
      syn_set_sync_header(&header);
      header.length = 0;
      sci->awaiting_reply = 1;
      count_sync++;
      sci->await_start_time = mdate();
      csend_all(sci->socket, &header, sizeof(header),
          sci->useless_vlc_object);
    }
  }

// lock is not held here
SynDestroyWithIniailize:
  if(sci->initialize_callback) {
    (*sci->initialize_callback)(rv, sci->initialize_param);
  }
  SynLock(sci);  // to be released immediately in SynDestroying

// lock is assumed to be held here
SynDestroying:
  // close socket
  shutdown(sci->socket, SHUT_RDWR);
  net_Close(sci->socket);

  // release mutex
  SynUnlock(sci);

  // wait until receive thread finishes
  if(sci->receive_thread_initialized) {
    vlc_join(sci->receive_thread, NULL);
  }

  // We now be only thread accessing this structure so no need
  // to acquire lock

  // Clean up send info
  while(NULL != sci->send_info_head) {
    SynConnection_SendInfo* send_info = sci->send_info_head;
    sci->send_info_head = send_info->next;
    if(NULL != send_info->callback) {
      (*send_info->callback)(-10, send_info->param);
    }
    free(send_info->buffer);
    free(send_info);
  }

  // call destroy callback, which is only set when destroy is called
  if(NULL != sci->destroy_callback) {
    (*sci->destroy_callback)(0, sci->destroy_param);
  }

  // free data structure
  sci->state = SYN_UNINITIALIZED;

  vlc_object_release(sci->useless_vlc_object);
  memset(&sci, 0, sizeof(*sci));

  return NULL;
}

// server == 1, client == 0
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
    int type) {
  assert(NULL != connection);
  assert(NULL != receive_callback);

  connection->index = -1;

  // Find empty internal descriptor
  int i;
  for(i = 0; i < MAX_CONNECTIONS; ++i) {
    if(SYN_UNINITIALIZED == syn_ci_[i].state) {
      connection->index = i;
      break;
    }
  }
  if(MAX_CONNECTIONS == i) {
    if(callback) {
      (*callback)(-1, param);
    }
    return -1;
  }
  memset(&syn_ci_[i], 0, sizeof(syn_ci_[i]));

  // Initialize internal structure variables
  // no need to acquire lock since we have not started
  // the thread yet
  vlc_mutex_init(&syn_ci_[i].lock);
  vlc_cond_init(&syn_ci_[i].send_info_non_empy);
  syn_ci_[i].relay_server_host = server_addr;
  syn_ci_[i].relay_server_port = server_port;
  syn_ci_[i].receive_callback = receive_callback;
  syn_ci_[i].receive_param = receive_param;
  syn_ci_[i].initialize_callback = callback;
  syn_ci_[i].initialize_param = param;
  syn_ci_[i].peer_connect_callback = peer_connect_callback;
  syn_ci_[i].peer_connect_param = peer_connect_param;
  syn_ci_[i].heartbeat_callback = heartbeat_callback;
  syn_ci_[i].heartbeat_param = heartbeat_param;
  syn_ci_[i].estimated_rtt = -1;
  syn_ci_[i].estimated_rtt_stdev = 0;
  syn_ci_[i].delta_t_initialized = 0;
  syn_ci_[i].delta_t = 0;
  syn_ci_[i].delta_t_confidence = 100000;
  syn_ci_[i].useless_vlc_object = vlc_custom_create(
      parent,
      sizeof( *syn_ci_[i].useless_vlc_object ),
      "syn_connection_dymmy" );


  if(type) {
    syn_ci_[i].state = SYN_SERVER_INITIALIZING;
  } else {
    syn_ci_[i].state = SYN_CLIENT_INITIALIZING;

    // check key is valid
    if(!SynConnection_IsAddrValid(addr)) {
      if(callback) {
        (*callback)(-2, param);
      }
      return -2;
    }


    int rv = char_to_uint64(&syn_ci_[i].address.relay_server_key, addr);
    assert(0 == rv);
  }

  // Create Send Thread
  int rv = vlc_clone(
      &syn_ci_[i].send_thread,
      &syn_send_thread,
      (void*)i, 
      SYN_THREAD_PRIORITY);
  if(0 == rv) {
    return 0;
  } else {
    if(callback) {
      (*callback)(-3, param);
    }
    return -3;
  }
}

int SynConnection_InitializeAsServer(
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
) {
  return SynConnection_InitializeHelper(
      parent,
      connection,
      0,  /* addr */
      server_addr, server_port,
      receive_callback,
      receive_param,
      host_key_callback, host_key_param,
      peer_connect_callback, peer_connect_param,
      heartbeat_callback, heartbeat_param,
      1  /* server */);
}

int SynConnection_InitializeAsClient(
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
) {
  return SynConnection_InitializeHelper(
      parent,
      connection,
      addr,
      server_addr, server_port,
      receive_callback, receive_param,
      0, 0,
      callback, param,
      heartbeat_callback, heartbeat_param,
      0 /* client */);
}

int SynConnection_Destroy(
    SynConnection connection,
    SynConnection_Callback* callback,
    void* param
) {
  int i = connection.index;
  if(i < 0 || i >= MAX_CONNECTIONS) {
    if(callback) {
      (*callback)(-1, param);
    }
    return -1;
  }
  if(SYN_UNINITIALIZED == syn_ci_[i].state ||
      SYN_DESTROYING == syn_ci_[i].state) {
    if(callback) {
      (*callback)(-2, param);
    }
    return -2;
  }
  SynConnectionInternal* sci = &syn_ci_[i];

  SynLock(sci);
    sci->state = SYN_DESTROYING;
    sci->destroy_callback = callback;
    sci->destroy_param = param;
  SynUnlock(sci);
  vlc_cond_signal(&sci->send_info_non_empy);
  vlc_join(sci->send_thread, NULL);

  return 0;
}

void syn_connection_append_send_info(
    SynConnectionInternal* sci,
    SynConnection_Callback* callback,
    void* param,
    int len,
    void* buffer,
    uint64_t flag,
    uint64_t timestamp) {
  SynConnection_SendInfo* send_info =
    malloc(sizeof(SynConnection_SendInfo));
  send_info->callback = callback;
  send_info->param = param;
  send_info->size = len + sizeof(SynSegmentHeader);
  send_info->next = NULL;
  send_info->buffer = malloc(len + sizeof(SynSegmentHeader));
  SynSegmentHeader *header = (SynSegmentHeader*)send_info->buffer;
  header->flag = flag;
  header->timestamp_reply = timestamp;
  header->length = len;
  if(len > 0) {
    memcpy(send_info->buffer + sizeof(SynSegmentHeader), buffer, len);
  }

  SynLock(sci);
    if(NULL == sci->send_info_head) {
      sci->send_info_head = sci->send_info_tail = send_info;
    } else {
      sci->send_info_tail->next = send_info;
      sci->send_info_tail = send_info;
    }
    vlc_cond_signal(&sci->send_info_non_empy);
  SynUnlock(sci);
}

int SynConnection_Send(
    SynConnection connection,
    void* buffer,
    size_t len,
    SynConnection_Callback* callback,
    void* param
) {
  int i = connection.index;
  if(i < 0 || i >= MAX_CONNECTIONS) {
    return -1;
  }
  if(SYN_INITIALIZED != syn_ci_[i].state) {
    return -2;
  }
  SynConnectionInternal* sci = &syn_ci_[i];

  syn_connection_append_send_info(
      sci, callback, param, len, buffer,
      SYNC_MASK | SYNC_END, 0);

  return 0;
}

size_t SynConnection_GetAddrLen(
    SynConnection connection
) {
  int i = connection.index;
  if(i < 0 || i >= MAX_CONNECTIONS) {
    return -1;
  }
  return SYN_KEY_BUFFER_LENGTH + 1;
}

int SynConnection_GetAddr(
    SynConnection connection,
    char* out_buffer,
    size_t len
) {
  int i = connection.index;
  if(i < 0 || i >= MAX_CONNECTIONS) {
    return -1;
  }
  if(len < SYN_KEY_BUFFER_LENGTH + 1) {
    return -3;
  }
  uint64_to_char(syn_ci_[i].address.relay_server_key, out_buffer);

  return 0;
}

mtime_t SynConnection_EstimatedDelayStdev(
    SynConnection connection) {
  int i = connection.index;
  if(i < 0 || i >= MAX_CONNECTIONS) {
    return -1;
  }
  if(SYN_INITIALIZED != syn_ci_[i].state) {
    return -2;
  }

  SynConnectionInternal* sci = &syn_ci_[i];
  mtime_t rv = 0;

  SynLock(sci);
  rv = sci->estimated_rtt_stdev;
  SynUnlock(sci);

  return rv;
}
