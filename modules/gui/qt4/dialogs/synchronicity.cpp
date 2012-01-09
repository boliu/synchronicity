/*****************************************************************************
  * Synchronicity.cpp : Synch dialog
 ****************************************************************************
 * Copyright (C) 2011 the Synchronicity team
 *
 * Authors: Graham Pinhey, Kevin Huff
 ****************************************************************************
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

#include "dialogs/synchronicity.hpp"
#include "util/qt_dirs.hpp"
#include "../qt4.hpp"
#include "../dialogs_provider.hpp"

#include <synchronicity/syn_connection.h>
#include <vlc_about.h>
#include <vlc_intf_strings.h>
#include <vlc_playlist.h>
#ifdef UPDATE_CHECK
# include <vlc_update.h>
#endif

#include <QTextBrowser>
#include <QTabWidget>
#include <QLabel>
#include <QString>
#include <QDialogButtonBox>
#include <QEvent>
#include <QFileDialog>
#include <QDate>
#include <QPushButton>

#include <assert.h>

SynchDialog::SynchDialog( intf_thread_t *_p_intf ) : QVLCFrame( _p_intf )

{
    setWindowTitle( qtr( "Connect to a peer" ) );
    setWindowRole( "vlc-help" );
    setMinimumSize( 200, 75 );
    setMaximumSize( 200, 75 );

    QGridLayout *layout = new QGridLayout( this );
    QPushButton *closeButton = new QPushButton( qtr( "&Close" ) );
    closeButton->setDefault( true );

    QPushButton *connectButton = new QPushButton ( qtr( "Connect" ) );
    QPushButton *hostButton = new QPushButton ( qtr( "Host" ) );
    connection_key_text = new QLineEdit( "" );

    layout->addWidget( connection_key_text, 0, 0, 1, 4 );
    layout->addWidget( closeButton, 1, 3 );
    layout->addWidget( connectButton, 1, 0, 1, 2 );
    layout->addWidget( hostButton, 1, 2 );

    BUTTONACT( connectButton, connectButton_Click() );
    BUTTONACT( hostButton, hostButton_Click() );
    BUTTONACT( closeButton, close() );
    readSettings( "Help", QSize( 500, 450 ) );
}

SynchDialog::~SynchDialog()
{
    writeSettings( "Help" );
    delete connection_key_text;
}

void SynchDialog::close()
{
    toggleVisible();
}

void SynchDialog::connectButton_Click()
{
    playlist_t *p_playlist = THEPL;
    playlist_SynConnect(p_playlist,
       connection_key_text->text().toAscii().data());
}

static void SetTextCallback(int rv, void* param) {
  DialogsProvider *dp = (DialogsProvider*)param;
  DialogEvent* event = new DialogEvent(INTF_DIALOG_SYNCHRONICITY, 0, 0);
  QApplication::postEvent(dp, event);
}

void SynchDialog::hostButton_Click()
{
   // this->connection = playlist_SynHost( THEPL, &SetTextCallback, THEDP );
}

void SynchDialog::synConnectionReady()
{
  size_t len = SynConnection_GetAddrLen(connection);
  char* addr = new char[len];
  SynConnection_GetAddr(connection, addr, len);
  connection_key_text->setText(addr);
  connection_key_text->setReadOnly(true);
  delete[] addr;
}
