/*****************************************************************************
 * synchronicity.c : Playlist management functions
 *****************************************************************************
 * Copyright © 2011-2011 the Synchronicity team
 * $Id$
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston MA 02110-1301, USA.
 *****************************************************************************/
#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <assert.h>

#include <synchronicity/syn_connection.h>
#include <synchronicity/syn_error_codes.h>
#include <synchronicity/syn_parsing.h>
#include <vlc_common.h>
#include <vlc_es.h>
#include <vlc_input.h>
#include <vlc_interface.h>
#include <vlc_playlist.h>
#include <vlc_rand.h>
#include "stream_output/stream_output.h"
#include "playlist_internal.h"


static void SynBreakConnection(playlist_t* p_playlist) {
  if(pl_priv(p_playlist)->b_syn_created) {
    var_SetInteger( p_playlist, "synchronicity", CONNECTION_FAILURE );
    SynConnection_Destroy(pl_priv(p_playlist)->syn_connection, NULL, NULL);
    pl_priv(p_playlist)->b_syn_created = false;
    pl_priv(p_playlist)->b_syn_can_send = false;
  }
}

static void SynReceiveCallback(int rv, mtime_t delay, void* param, void* buffer, size_t len) {
  playlist_t* p_playlist = (playlist_t*)param;
  pl_priv(p_playlist)->b_syn_can_send = false;
  if (rv < 0) {
    // Connection closed.
    SynBreakConnection(p_playlist);
    return;
  }

  if(0 == len) {
    playlist_SynDisconnect(p_playlist);
    return;
  }

  input_thread_t* p_in = playlist_CurrentInput(p_playlist);
  if (NULL != p_in) {
    SynCommand syn = CommandFromString(buffer, len);
    mtime_t current;
    input_Control(p_in, INPUT_GET_TIME, &current);
    switch(syn.type) {
      case SYNCOMMAND_PLAY:
        input_Control(p_in, INPUT_SET_STATE, PLAYING_S);
        delay = syn.data.i_time + delay - current;
        break;
      case SYNCOMMAND_PAUSE:
        input_Control(p_in, INPUT_SET_STATE, PAUSE_S);
        delay = syn.data.i_time - current;
        break;
      case SYNCOMMAND_SEEK:
        {
          int playpause;
          input_Control(p_in, INPUT_GET_STATE, &playpause);

          switch (playpause) {
            case PLAYING_S:
              delay = syn.data.i_time + delay - current;
              break;
            case PAUSE_S:
              delay = syn.data.i_time - current;
              break;
            default:
              msg_Err(p_playlist, "SynReceiveCallback: Received on no input? %d", playpause);
              delay = 0;
              break;
          }
        }
        break;
      case SYNCOMMAND_MYNAMEIS:
        {
          char *username = syn.message;
          //warning this pointer will be invalid as soon as this method exits
          var_SetString( p_playlist, "synchronicity-user", username);
        }
        break;
      default:
        msg_Info(p_playlist, "SynReceiveCallback: Error");
        delay = 0;
        break;
    }
    if (0 != delay && SYNCOMMAND_MYNAMEIS != syn.type) {
      pl_priv(p_playlist)->t_wall_minus_video = mdate() - (current + delay);
      var_SetInteger( p_playlist, "synchronicity", PEER_SNAP );
      input_Control(p_in, INPUT_SET_TIME, current + delay);
    }
    vlc_object_release(p_in);
  } else { // move outside?
    SynCommand syn = CommandFromString(buffer, len);
    if (SYNCOMMAND_MYNAMEIS == syn.type)
    {
      char *username = syn.message;
      //warning this pointer will be invalid as soon as this method exits
      var_SetString( p_playlist, "synchronicity-user", username);
    }
  }
  pl_priv(p_playlist)->b_syn_can_send = true;
}

static void SynFreeMemory(int rv, void* param) {
  VLC_UNUSED(rv);
  free(param);
}

static int SendSynCommand(playlist_t* p_playlist, SynCommand syn) {
  playlist_private_t *p_sys = pl_priv(p_playlist);
  if(SYNCOMMAND_MYNAMEIS != syn.type) {
    p_sys->t_wall_minus_video = mdate() - syn.data.i_time;
  }
  if(!p_sys->b_syn_can_send) {
    return VLC_SUCCESS;
  }
  char* buffer = calloc(sizeof(char), 80);
  int numbytes = StringFromCommand(syn, buffer, 80);
  if (numbytes > 0) {
    return SynConnection_Send(
        p_sys->syn_connection,
        buffer, numbytes,
        &SynFreeMemory, buffer);
  }
  return VLC_EGENERIC;
}

static void SynConnectCallback(int rv, void* param) {
  playlist_t* p_playlist = (playlist_t*)param;
  if (rv < 0) {
    // Connection closed.
    SynBreakConnection(p_playlist);
    var_SetInteger( p_playlist, "synchronicity", CONNECTION_FAILURE );
    return;
  }

  var_SetInteger( p_playlist, "synchronicity", CONNECT_SUCCESS);
  pl_priv(param)->b_syn_can_send = true;

  char *username = pl_priv(p_playlist)->psz_syn_user;

  SynCommand syn;
  syn.type = SYNCOMMAND_MYNAMEIS;
  strncpy(&syn.message, username, sizeof(syn.message));

  SendSynCommand(p_playlist, syn);

}

void playlist_SynConnect(playlist_t * p_playlist, const char* addr) {
  playlist_private_t *p_sys = pl_priv(p_playlist);
  if(!p_sys->b_syn_created) {
    int rv = SynConnection_InitializeAsClient(
        (vlc_object_t*)p_playlist,
        &p_sys->syn_connection,
        addr,
        pl_priv(p_playlist)->psz_syn_server_host,
        pl_priv(p_playlist)->i_syn_port,
        &SynReceiveCallback, p_playlist,
        &SynConnectCallback, p_playlist);
    if (rv < 0) {
      var_SetInteger( p_playlist, "synchronicity", CONNECTION_FAILURE );
    } else {
      p_sys->b_syn_created = true;
    }
  }
}

void playlist_SynDisconnect(playlist_t* p_playlist) {
  if(pl_priv(p_playlist)->b_syn_created) {
    var_SetInteger( p_playlist, "synchronicity", PEER_DISCONNECT);
    SynConnection_Destroy(pl_priv(p_playlist)->syn_connection, NULL, NULL);
    pl_priv(p_playlist)->b_syn_created = false;
    pl_priv(p_playlist)->b_syn_can_send = false;
  }
}

static void SynHostCallback(int rv, void* param) {
  playlist_t* p_playlist = (playlist_t*)param;
  if (rv < 0) {
    // Connection closed.
    SynBreakConnection(p_playlist);
    return;
  }
  var_SetInteger( p_playlist, "synchronicity", HOST_SUCCESS );
}

static void SynClientConnectedCallback(int rv, void* param) {
  playlist_t* p_playlist = (playlist_t*)param;
  if (rv < 0) {
    // Connection closed.
    SynBreakConnection(p_playlist);
    return;
  }
  var_SetInteger( p_playlist, "synchronicity", CLIENT_CONNECTED );
  pl_priv(p_playlist)->b_syn_can_send = true;

  // Snap client to host
  input_thread_t* p_in = playlist_CurrentInput(p_playlist);
  if (NULL != p_in) {
    SynCommand sync_to_host;
    input_Control(p_in, INPUT_GET_TIME, &sync_to_host.data.i_time);
    int playpause;
    input_Control(p_in, INPUT_GET_STATE, &playpause);
    vlc_object_release(p_in);

    switch (playpause) {
      case PLAYING_S:
        sync_to_host.type = SYNCOMMAND_PLAY;
        break;
      case PAUSE_S:
        sync_to_host.type = SYNCOMMAND_PAUSE;
        break;
      default:
        sync_to_host.type = SYNCOMMAND_ERROR;
        break;
    }
    rv = 0;
    rv |= SendSynCommand(p_playlist, sync_to_host);


    if (rv < 0) {
      // Connection closed.
      SynBreakConnection(p_playlist);
      return;
    }
  }
  char *username = pl_priv(p_playlist)->psz_syn_user;

  SynCommand syn;
  syn.type = SYNCOMMAND_MYNAMEIS;
  strncpy(&syn.message, username, sizeof(syn.message));
  syn.message[MESSAGE_LENGTH-1] = 0; // null terminate string

  rv |= SendSynCommand(p_playlist, syn);
  if (rv < 0) {
    // Connection closed.
    SynBreakConnection(p_playlist);
    return;
  }
}

void playlist_SynHost(playlist_t * p_playlist) {
  if(!pl_priv(p_playlist)->b_syn_created) {
    int rv = SynConnection_InitializeAsServer(
        (vlc_object_t*)p_playlist,
        &pl_priv(p_playlist)->syn_connection,
        pl_priv(p_playlist)->psz_syn_server_host,
        pl_priv(p_playlist)->i_syn_port,
        &SynReceiveCallback, p_playlist,
        &SynHostCallback, p_playlist,
        &SynClientConnectedCallback, p_playlist);
    if (rv < 0) {
      var_SetInteger( p_playlist, "synchronicity", CONNECTION_FAILURE );
    } else {
      pl_priv(p_playlist)->b_syn_created = true;
    }
    //success, set synchronicity variable
  }
}

size_t playlist_SynGetHostAddrLen(playlist_t * p_playlist) {
  size_t len = 0;
  if(pl_priv(p_playlist)->b_syn_created) {
    SynConnection connection = pl_priv(p_playlist)->syn_connection;
    len = SynConnection_GetAddrLen(connection);
  }
  return len;
}

void playlist_SynGetHostAddr(playlist_t * p_playlist, char* addr, size_t len) {
  if(pl_priv(p_playlist)->b_syn_created) {
    SynConnection connection = pl_priv(p_playlist)->syn_connection;
    if (len > 0) {
      SynConnection_GetAddr(connection, addr, len);
    }
  }
}
