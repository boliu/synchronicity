/*****************************************************************************
 * thread.c : Playlist management functions
 *****************************************************************************
 * Copyright © 1999-2008 VLC authors and VideoLAN
 * $Id$
 *
 * Authors: Samuel Hocevar <sam@zoy.org>
 *          Clément Stenac <zorglub@videolan.org>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation; either version 2.1 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston MA 02110-1301, USA.
 *****************************************************************************/
#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <assert.h>

#include <synchronicity/syn_connection.h>
#include <synchronicity/syn_parsing.h>
#include <synchronicity/syn_error_codes.h>
#include <vlc_common.h>
#include <vlc_es.h>
#include <vlc_input.h>
#include <vlc_interface.h>
#include <vlc_playlist.h>
#include <vlc_rand.h>
#include "stream_output/stream_output.h"
#include "playlist_internal.h"

/*****************************************************************************
 * Local prototypes
 *****************************************************************************/
static void *Thread   ( void * );

/*****************************************************************************
 * Main functions for the global thread
 *****************************************************************************/

/**
 * Create the main playlist threads.
 * Additionally to the playlist, this thread controls :
 *    - Statistics
 *    - VLM
 * \param p_parent
 * \return an object with a started thread
 */
void playlist_Activate( playlist_t *p_playlist )
{
    /* */
    playlist_private_t *p_sys = pl_priv(p_playlist);

    /* Start the playlist thread */
    if( vlc_clone( &p_sys->thread, Thread, p_playlist,
                   VLC_THREAD_PRIORITY_LOW ) )
    {
        msg_Err( p_playlist, "cannot spawn playlist thread" );
    }
    msg_Dbg( p_playlist, "playlist threads correctly activated" );
}

void playlist_Deactivate( playlist_t *p_playlist )
{
    /* */
    playlist_private_t *p_sys = pl_priv(p_playlist);

    msg_Dbg( p_playlist, "deactivating the playlist" );

    playlist_SynDisconnect(p_playlist);

    PL_LOCK;
    vlc_object_kill( p_playlist );
    vlc_cond_signal( &p_sys->signal );
    PL_UNLOCK;

    vlc_join( p_sys->thread, NULL );
    assert( !p_sys->p_input );

    /* release input resources */
    if( p_sys->p_input_resource )
    {
        input_resource_Terminate( p_sys->p_input_resource );
        input_resource_Release( p_sys->p_input_resource );
    }
    p_sys->p_input_resource = NULL;

    if( var_InheritBool( p_playlist, "media-library" ) )
        playlist_MLDump( p_playlist );

    PL_LOCK;

    /* Release the current node */
    set_current_status_node( p_playlist, NULL );

    /* Release the current item */
    set_current_status_item( p_playlist, NULL );

    PL_UNLOCK;

    msg_Dbg( p_playlist, "playlist correctly deactivated" );
}

/* */

/* Input Callback */
static int InputEvent( vlc_object_t *p_this, char const *psz_cmd,
                       vlc_value_t oldval, vlc_value_t newval, void *p_data )
{
    VLC_UNUSED(p_this); VLC_UNUSED(psz_cmd); VLC_UNUSED(oldval);
    playlist_t *p_playlist = p_data;

    if( newval.i_int != INPUT_EVENT_STATE &&
        newval.i_int != INPUT_EVENT_DEAD )
        return VLC_SUCCESS;

    PL_LOCK;

    /* XXX: signaling while not changing any parameter... suspicious... */
    vlc_cond_signal( &pl_priv(p_playlist)->signal );

    PL_UNLOCK;
    return VLC_SUCCESS;
}

static void UpdateActivity( playlist_t *p_playlist, int i_delta )
{
    PL_ASSERT_LOCKED;

    const int i_activity = var_GetInteger( p_playlist, "activity" ) ;
    var_SetInteger( p_playlist, "activity", i_activity + i_delta );
}

/**
 * Synchronise the current index of the playlist
 * to match the index of the current item.
 *
 * \param p_playlist the playlist structure
 * \param p_cur the current playlist item
 * \return nothing
 */
static void ResyncCurrentIndex( playlist_t *p_playlist, playlist_item_t *p_cur )
{
    PL_ASSERT_LOCKED;

    PL_DEBUG( "resyncing on %s", PLI_NAME( p_cur ) );
    /* Simply resync index */
    int i;
    p_playlist->i_current_index = -1;
    for( i = 0 ; i< p_playlist->current.i_size; i++ )
    {
        if( ARRAY_VAL( p_playlist->current, i ) == p_cur )
        {
            p_playlist->i_current_index = i;
            break;
        }
    }
    PL_DEBUG( "%s is at %i", PLI_NAME( p_cur ), p_playlist->i_current_index );
}

static void ResetCurrentlyPlaying( playlist_t *p_playlist,
                                   playlist_item_t *p_cur )
{
    playlist_private_t *p_sys = pl_priv(p_playlist);

    stats_TimerStart( p_playlist, "Items array build",
                      STATS_TIMER_PLAYLIST_BUILD );
    PL_DEBUG( "rebuilding array of current - root %s",
              PLI_NAME( p_sys->status.p_node ) );
    ARRAY_RESET( p_playlist->current );
    p_playlist->i_current_index = -1;
    for( playlist_item_t *p_next = NULL; ; )
    {
        /** FIXME: this is *slow* */
        p_next = playlist_GetNextLeaf( p_playlist,
                                       p_sys->status.p_node,
                                       p_next, true, false );
        if( !p_next )
            break;

        if( p_next == p_cur )
            p_playlist->i_current_index = p_playlist->current.i_size;
        ARRAY_APPEND( p_playlist->current, p_next);
    }
    PL_DEBUG("rebuild done - %i items, index %i", p_playlist->current.i_size,
                                                  p_playlist->i_current_index);

    if( var_GetBool( p_playlist, "random" ) && ( p_playlist->current.i_size > 0 ) )
    {
        /* Shuffle the array */
        for( unsigned j = p_playlist->current.i_size - 1; j > 0; j-- )
        {
            unsigned i = vlc_lrand48() % (j+1); /* between 0 and j */
            playlist_item_t *p_tmp;
            /* swap the two items */
            p_tmp = ARRAY_VAL(p_playlist->current, i);
            ARRAY_VAL(p_playlist->current,i) = ARRAY_VAL(p_playlist->current,j);
            ARRAY_VAL(p_playlist->current,j) = p_tmp;
        }
    }
    p_sys->b_reset_currently_playing = false;
    stats_TimerStop( p_playlist, STATS_TIMER_PLAYLIST_BUILD );
}

// Debugging callback
static int StateListener( vlc_object_t *p_this, const char *psz_var,
                            vlc_value_t oldval, vlc_value_t newval, void *param ) {
  VLC_UNUSED(oldval);
  VLC_UNUSED(param);

  msg_Info(p_this, "%s", psz_var);
  SynCommand syn;
  char buffer[80];
  int numbytes;
  switch (newval.i_int)
  {
  case PLAYING_S:
    msg_Info(p_this, "Playing");
    syn.type = SYNCOMMAND_PLAY;
    input_Control((input_thread_t*)p_this, INPUT_GET_TIME, &syn.data.i_time);
    numbytes = StringFromCommand(syn, buffer, 80);
    if(numbytes > 0) {
      buffer[numbytes] = '\0';
      msg_Info(p_this, "%s", buffer);
      sprintf(buffer, "%d", syn.data.i_time);
      msg_Info(p_this, "%s", buffer);
    } else {
      msg_Info(p_this, "Problem w/Play");
    }
    break;
  case PAUSE_S:
    msg_Info(p_this, "Paused");
    break;
  }
  //var_SetFloat(p_this, "position", 0.5f);
  return VLC_SUCCESS;
}

static int PositionListener( vlc_object_t *p_this, const char *psz_var,
                            vlc_value_t oldval, vlc_value_t newval, void *param ) {
  VLC_UNUSED(oldval);
  VLC_UNUSED(param);

  msg_Info(p_this, "%s", psz_var);
  char formatted[30];
  sprintf(formatted, "%f", newval.f_float);
  msg_Info(p_this, "%s", formatted);
  //var_SetInteger(p_this, "state", PAUSE_S);

  SynCommand syn;
  char buffer[80];
  int numbytes;
  syn.type = SYNCOMMAND_SEEK;
  input_Control((input_thread_t*)p_this, INPUT_GET_TIME, &syn.data.i_time);
  numbytes = StringFromCommand(syn, buffer, 80);
  if(numbytes > 0) {
    buffer[numbytes] = '\0';
    msg_Info(p_this, "%s", buffer);
  }
  SynCommand test;
  test = CommandFromString(buffer, 80);
  switch(test.type) {
    case SYNCOMMAND_SEEK:
      msg_Info(p_this, "Successful decode");
      sprintf(formatted, "%i", test.data.i_time);
      msg_Info(p_this, "%s", formatted);
      break;
    default:
      msg_Info(p_this, "Problem");
      break;
  }
  return VLC_SUCCESS;
}

/*****************************************
 * Synchronicity callback functions
 ****************************************/

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
  int numbytes;
  numbytes = StringFromCommand(syn, buffer, 80);
  if (numbytes > 0) {
    SynConnection_Send(
        p_sys->syn_connection,
        buffer, numbytes,
        &SynFreeMemory, buffer);
    return VLC_SUCCESS;
  }
  msg_Info(p_playlist, "SendSynCommand: Error");
  return VLC_EGENERIC;
}

static int SynStateListener( vlc_object_t *p_this, const char *psz_var,
                           vlc_value_t oldval, vlc_value_t newval,
                           void *param ) {
  VLC_UNUSED(p_this);
  VLC_UNUSED(psz_var);
  VLC_UNUSED(oldval);

  SynCommand syn;
  switch (newval.i_int) {
    case PLAYING_S:
      syn.type = SYNCOMMAND_PLAY;
      break;
    case PAUSE_S:
      syn.type = SYNCOMMAND_PAUSE;
      break;
    default:
      return VLC_EGENERIC;
  }
  input_Control((input_thread_t*)p_this, INPUT_GET_TIME, &syn.data.i_time);
  return SendSynCommand((playlist_t*)param, syn);
}

static int SynEventListener( vlc_object_t *p_this, const char *psz_var,
                           vlc_value_t oldval, vlc_value_t newval,
                           void *param ) {
  playlist_private_t* p_playlist = pl_priv((playlist_t*)param);
  if(!p_playlist->b_syn_can_send) {
    return VLC_SUCCESS;
  }
  if(INPUT_EVENT_POSITION == newval.i_int ) {
    // calculate current wall clock - video time
    mtime_t current_wall = mdate();
    mtime_t current_time;
    input_Control((input_thread_t*)p_this, INPUT_GET_TIME, &current_time);
    mtime_t current_difference = current_wall - current_time;

    if(p_playlist->b_need_send_seek) {
      p_playlist->b_need_send_seek = false;
      SynCommand syn;
      syn.type = SYNCOMMAND_SEEK;
      syn.data.i_time = current_time;
      return SendSynCommand((playlist_t*)param, syn);
    } else {
      // immediately after a position change, the difference is totally messed up, so
      // this is in the else block
      int playpause;
      input_Control((input_thread_t*)p_this, INPUT_GET_STATE, &playpause);
      if(p_playlist->t_wall_minus_video
        && !p_playlist->b_correcting
        && PLAYING_S == playpause
        //&& current_wall - p_playlist->t_last_correction_time > 200000
        ) {
        mtime_t diff_diff = current_difference - p_playlist->t_wall_minus_video;
        if(diff_diff > p_playlist->offline_sync_threshold
          || diff_diff < -p_playlist->offline_sync_threshold) {
          p_playlist->t_last_correction_time = current_wall;
          p_playlist->last_diff_diff = (4 * diff_diff + p_playlist->last_diff_diff) / 5;

          p_playlist->b_correcting = true;
          input_Control((input_thread_t*)p_this, INPUT_SET_TIME, current_time +
              p_playlist->last_diff_diff);
          p_playlist->b_correcting = false;
        }
      }
    }
  }
  return VLC_SUCCESS;
}

static int SynPositionListener( vlc_object_t *p_this, const char *psz_var,
                           vlc_value_t oldval, vlc_value_t newval,
                           void *param ) {
  VLC_UNUSED(p_this);
  VLC_UNUSED(psz_var);
  VLC_UNUSED(oldval);

  pl_priv((playlist_t*)param)->b_need_send_seek = true;
  return VLC_SUCCESS;
}

/**
 * Start the input for an item
 *
 * \param p_playlist the playlist object
 * \param p_item the item to play
 * \return nothing
 */
static int PlayItem( playlist_t *p_playlist, playlist_item_t *p_item )
{
    playlist_private_t *p_sys = pl_priv(p_playlist);
    input_item_t *p_input = p_item->p_input;

    PL_ASSERT_LOCKED;

    msg_Dbg( p_playlist, "creating new input thread" );

    p_input->i_nb_played++;
    set_current_status_item( p_playlist, p_item );

    p_sys->status.i_status = PLAYLIST_RUNNING;

    UpdateActivity( p_playlist, DEFAULT_INPUT_ACTIVITY );

    assert( p_sys->p_input == NULL );

    if( !p_sys->p_input_resource )
        p_sys->p_input_resource = input_resource_New( VLC_OBJECT( p_playlist ) );
    input_thread_t *p_input_thread = input_Create( p_playlist, p_input, NULL, p_sys->p_input_resource );
    if( p_input_thread )
    {
        p_sys->p_input = p_input_thread;
        var_AddCallback( p_input_thread, "intf-event", InputEvent, p_playlist );

        // Add Synchronicity callbacks
        //var_AddCallback( p_input_thread, "state", StateListener, p_input_thread );
        var_AddCallback( p_input_thread, "state", SynStateListener, p_playlist );
        var_AddCallback( p_input_thread, "position", SynPositionListener, p_playlist );
        var_AddCallback( p_input_thread, "intf-event", SynEventListener, p_playlist);
        //var_AddCallback( p_input_thread, "position", PositionListener, p_input_thread );


        if(p_sys->b_syn_created && var_GetBool( p_playlist, "repeat" ) /* loop one */) {
          // continuing a loop single video while connected
          p_sys->b_need_send_seek = true;
        } else {
          //set synchronicity variable to enable gui
          var_SetInteger( p_playlist, "synchronicity", ITEM_PLAYING);

          // Re-initialize synchronicity variables on every playlist item
          p_sys->b_syn_can_send = false;
          p_sys->b_syn_created = false;
          p_sys->b_need_send_seek = false;
        }

        p_sys->psz_syn_server_host =
          var_InheritString( p_playlist->p_libvlc, "synchronicity-server" );
        p_sys->i_syn_port =
          var_InheritInteger( p_playlist->p_libvlc, "synchronicity-port" );
        p_sys->psz_syn_user =
          var_InheritString( p_playlist->p_libvlc, "synchronicity-user" );
        int threshold_in_ms = var_InheritInteger( p_playlist->p_libvlc, "synchronicity-offline-sync-threshold" );
        if(threshold_in_ms < 20) threshold_in_ms = 20;
        p_sys->offline_sync_threshold = threshold_in_ms * 1000;

        var_SetAddress( p_playlist, "input-current", p_input_thread );

        if( input_Start( p_sys->p_input ) )
        {
            vlc_object_release( p_input_thread );
            p_sys->p_input = p_input_thread = NULL;
        }
    }

    char *psz_uri = input_item_GetURI( p_item->p_input );
    if( psz_uri && ( !strncmp( psz_uri, "directory:", 10 ) ||
                     !strncmp( psz_uri, "vlc:", 4 ) ) )
    {
        free( psz_uri );
        return VLC_SUCCESS;
    }
    free( psz_uri );

    /* TODO store art policy in playlist private data */
    if( var_GetInteger( p_playlist, "album-art" ) == ALBUM_ART_WHEN_PLAYED )
    {
        bool b_has_art;

        char *psz_arturl, *psz_name;
        psz_arturl = input_item_GetArtURL( p_input );
        psz_name = input_item_GetName( p_input );

        /* p_input->p_meta should not be null after a successfull CreateThread */
        b_has_art = !EMPTY_STR( psz_arturl );

        if( !b_has_art || strncmp( psz_arturl, "attachment://", 13 ) )
        {
            PL_DEBUG( "requesting art for %s", psz_name );
            playlist_AskForArtEnqueue( p_playlist, p_input );
        }
        free( psz_arturl );
        free( psz_name );
    }
    /* FIXME: this is not safe !!*/
    PL_UNLOCK;
    var_SetAddress( p_playlist, "item-current", p_input );
    PL_LOCK;

    return VLC_SUCCESS;
}

/**
 * Compute the next playlist item depending on
 * the playlist course mode (forward, backward, random, view,...).
 *
 * \param p_playlist the playlist object
 * \return nothing
 */
static playlist_item_t *NextItem( playlist_t *p_playlist )
{
    playlist_private_t *p_sys = pl_priv(p_playlist);
    playlist_item_t *p_new = NULL;

    /* Handle quickly a few special cases */
    /* No items to play */
    if( p_playlist->items.i_size == 0 )
    {
        msg_Info( p_playlist, "playlist is empty" );
        return NULL;
    }

    /* Start the real work */
    if( p_sys->request.b_request )
    {
        p_new = p_sys->request.p_item;
        int i_skip = p_sys->request.i_skip;
        PL_DEBUG( "processing request item: %s, node: %s, skip: %i",
                        PLI_NAME( p_sys->request.p_item ),
                        PLI_NAME( p_sys->request.p_node ), i_skip );

        if( p_sys->request.p_node &&
            p_sys->request.p_node != get_current_status_node( p_playlist ) )
        {

            set_current_status_node( p_playlist, p_sys->request.p_node );
            p_sys->request.p_node = NULL;
            p_sys->b_reset_currently_playing = true;
        }

        /* If we are asked for a node, go to it's first child */
        if( i_skip == 0 && ( p_new == NULL || p_new->i_children != -1 ) )
        {
            i_skip++;
            if( p_new != NULL )
            {
                p_new = playlist_GetNextLeaf( p_playlist, p_new, NULL, true, false );
                for( int i = 0; i < p_playlist->current.i_size; i++ )
                {
                    if( p_new == ARRAY_VAL( p_playlist->current, i ) )
                    {
                        p_playlist->i_current_index = i;
                        i_skip = 0;
                    }
                }
            }
        }

        if( p_sys->b_reset_currently_playing )
            /* A bit too bad to reset twice ... */
            ResetCurrentlyPlaying( p_playlist, p_new );
        else if( p_new )
            ResyncCurrentIndex( p_playlist, p_new );
        else
            p_playlist->i_current_index = -1;

        if( p_playlist->current.i_size && (i_skip > 0) )
        {
            if( p_playlist->i_current_index < -1 )
                p_playlist->i_current_index = -1;
            for( int i = i_skip; i > 0 ; i-- )
            {
                p_playlist->i_current_index++;
                if( p_playlist->i_current_index >= p_playlist->current.i_size )
                {
                    PL_DEBUG( "looping - restarting at beginning of node" );
                    p_playlist->i_current_index = 0;
                }
            }
            p_new = ARRAY_VAL( p_playlist->current,
                               p_playlist->i_current_index );
        }
        else if( p_playlist->current.i_size && (i_skip < 0) )
        {
            for( int i = i_skip; i < 0 ; i++ )
            {
                p_playlist->i_current_index--;
                if( p_playlist->i_current_index <= -1 )
                {
                    PL_DEBUG( "looping - restarting at end of node" );
                    p_playlist->i_current_index = p_playlist->current.i_size-1;
                }
            }
            p_new = ARRAY_VAL( p_playlist->current,
                               p_playlist->i_current_index );
        }
        /* Clear the request */
        p_sys->request.b_request = false;
    }
    /* "Automatic" item change ( next ) */
    else
    {
        bool b_loop = var_GetBool( p_playlist, "loop" );
        bool b_repeat = var_GetBool( p_playlist, "repeat" );
        bool b_playstop = var_GetBool( p_playlist, "play-and-stop" );

        /* Repeat and play/stop */
        if( b_repeat && get_current_status_item( p_playlist ) )
        {
            msg_Dbg( p_playlist,"repeating item" );
            return get_current_status_item( p_playlist );
        }
        if( b_playstop )
        {
            msg_Dbg( p_playlist,"stopping (play and stop)" );
            return NULL;
        }

        /* */
        if( get_current_status_item( p_playlist ) )
        {
            playlist_item_t *p_parent = get_current_status_item( p_playlist );
            while( p_parent )
            {
                if( p_parent->i_flags & PLAYLIST_SKIP_FLAG )
                {
                    msg_Dbg( p_playlist, "blocking item, stopping") ;
                    return NULL;
                }
                p_parent = p_parent->p_parent;
            }
        }

        PL_DEBUG( "changing item without a request (current %i/%i)",
                  p_playlist->i_current_index, p_playlist->current.i_size );
        /* Cant go to next from current item */
        if( get_current_status_item( p_playlist ) &&
            get_current_status_item( p_playlist )->i_flags & PLAYLIST_SKIP_FLAG )
            return NULL;

        if( p_sys->b_reset_currently_playing )
            ResetCurrentlyPlaying( p_playlist,
                                   get_current_status_item( p_playlist ) );

        p_playlist->i_current_index++;
        assert( p_playlist->i_current_index <= p_playlist->current.i_size );
        if( p_playlist->i_current_index == p_playlist->current.i_size )
        {
            if( !b_loop || p_playlist->current.i_size == 0 )
                return NULL;
            p_playlist->i_current_index = 0;
        }
        PL_DEBUG( "using item %i", p_playlist->i_current_index );
        if ( p_playlist->current.i_size == 0 )
            return NULL;

        p_new = ARRAY_VAL( p_playlist->current, p_playlist->i_current_index );
        /* The new item can't be autoselected  */
        if( p_new != NULL && p_new->i_flags & PLAYLIST_SKIP_FLAG )
            return NULL;
    }
    return p_new;
}

static int LoopInput( playlist_t *p_playlist )
{
    playlist_private_t *p_sys = pl_priv(p_playlist);
    input_thread_t *p_input = p_sys->p_input;

    if( !p_input )
        return VLC_EGENERIC;

    if( ( p_sys->request.b_request || !vlc_object_alive( p_playlist ) ) && !p_input->b_die )
    {
        PL_DEBUG( "incoming request - stopping current input" );
        input_Stop( p_input, true );
    }

    /* This input is dead. Remove it ! */
    if( p_input->b_dead )
    {
        PL_DEBUG( "dead input" );

        PL_UNLOCK;
        /* We can unlock as we return VLC_EGENERIC (no event will be lost) */

        /* input_resource_t must be manipulated without playlist lock */
        if( !var_CreateGetBool( p_input, "sout-keep" ) )
            input_resource_TerminateSout( p_sys->p_input_resource );

        /* The DelCallback must be issued without playlist lock */
        var_DelCallback( p_input, "intf-event", InputEvent, p_playlist );

        // Delete Synchronicity callbacks
        //var_DelCallback( p_input, "state", StateListener, p_input );
        var_DelCallback( p_input, "state", SynStateListener, p_playlist );
        var_DelCallback( p_input, "position", SynPositionListener, p_playlist );
        var_DelCallback( p_input, "intf-event", SynEventListener, p_playlist );
        //var_DelCallback( p_input, "position", PositionListener, p_input );

        // Disconnect when ends
        bool b_repeat = var_GetBool( p_playlist, "repeat" ) /* loop one */;
        if(p_sys->b_syn_created && !b_repeat ) {
          var_SetInteger( p_playlist, "synchronicity", PEER_DISCONNECT);
          SynConnection_Destroy(p_sys->syn_connection, NULL, NULL);
          p_sys->b_syn_created = false;

          //set synchronicity variable to disable GUI
          var_SetInteger( p_playlist, "synchronicity", ITEM_STOPPED);
        }
        free(p_sys->psz_syn_server_host);
        free(p_sys->psz_syn_user);

        PL_LOCK;

        p_sys->p_input = NULL;
        input_Close( p_input );

        UpdateActivity( p_playlist, -DEFAULT_INPUT_ACTIVITY );

        return VLC_EGENERIC;
    }
    /* This input is dying, let it do */
    else if( p_input->b_die )
    {
        PL_DEBUG( "dying input" );
    }
    /* This input has finished, ask it to die ! */
    else if( p_input->b_error || p_input->b_eof )
    {
        PL_DEBUG( "finished input" );
        input_Stop( p_input, false );
    }

    return VLC_SUCCESS;
}

static void LoopRequest( playlist_t *p_playlist )
{
    playlist_private_t *p_sys = pl_priv(p_playlist);
    assert( !p_sys->p_input );

    /* No input. Several cases
     *  - No request, running status -> start new item
     *  - No request, stopped status -> collect garbage
     *  - Request, running requested -> start new item
     *  - Request, stopped requested -> collect garbage
    */
    const int i_status = p_sys->request.b_request ?
                         p_sys->request.i_status : p_sys->status.i_status;

    if( i_status == PLAYLIST_STOPPED || !vlc_object_alive( p_playlist ) )
    {
        p_sys->status.i_status = PLAYLIST_STOPPED;

        if( p_sys->p_input_resource &&
            input_resource_HasVout( p_sys->p_input_resource ) )
        {
            /* XXX We can unlock if we don't issue the wait as we will be
             * call again without anything else done between the calls */
            PL_UNLOCK;

            /* input_resource_t must be manipulated without playlist lock */
            input_resource_TerminateVout( p_sys->p_input_resource );

            PL_LOCK;
        }
        else
        {
            if( vlc_object_alive( p_playlist ) )
                vlc_cond_wait( &p_sys->signal, &p_sys->lock );
        }
        return;
    }

    playlist_item_t *p_item = NextItem( p_playlist );
    if( p_item )
    {
        msg_Dbg( p_playlist, "starting playback of the new playlist item" );
        PlayItem( p_playlist, p_item );
        return;
    }

    msg_Dbg( p_playlist, "nothing to play" );
    p_sys->status.i_status = PLAYLIST_STOPPED;

    if( var_GetBool( p_playlist, "play-and-exit" ) )
    {
        msg_Info( p_playlist, "end of playlist, exiting" );
        libvlc_Quit( p_playlist->p_libvlc );
    }
}

/**
 * Run the main control thread itself
 */
static void *Thread ( void *data )
{
    playlist_t *p_playlist = data;
    playlist_private_t *p_sys = pl_priv(p_playlist);

    playlist_Lock( p_playlist );
    while( vlc_object_alive( p_playlist ) || p_sys->p_input )
    {
        /* FIXME: what's that ! */
        if( p_sys->b_reset_currently_playing &&
            mdate() - p_sys->last_rebuild_date > 30000 ) // 30 ms
        {
            ResetCurrentlyPlaying( p_playlist,
                                   get_current_status_item( p_playlist ) );
            p_sys->last_rebuild_date = mdate();
        }

        /* If there is an input, check that it doesn't need to die. */
        while( !LoopInput( p_playlist ) )
            vlc_cond_wait( &p_sys->signal, &p_sys->lock );

        LoopRequest( p_playlist );
    }
    playlist_Unlock( p_playlist );

    return NULL;
}

