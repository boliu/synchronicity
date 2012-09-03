/*****************************************************************************
 * Synchronicity.hpp : Synch dialog
 ****************************************************************************
 * Copyright (C) 2011 the Synchronicity team
 *
 * Authors: Graham Pinhey
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

#ifndef QVLC_SYNCH_DIALOG_H_
#define QVLC_SYNCH_DIALOG_H_ 1

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "qt4.hpp"

#include "util/qvlcframe.hpp"
#include "util/singleton.hpp"

#include <QLineEdit>

#include <synchronicity/syn_connection.h>

class QPushButton;
class QTextBrowser;
class QLabel;
class QEvent;
class QPushButton;
class QTextEdit;

class SynchDialog : public QVLCFrame, public Singleton<SynchDialog>
{
    Q_OBJECT
private:
    QLineEdit *connection_key_text;
    SynConnection connection;
    SynchDialog( intf_thread_t * );
    virtual ~SynchDialog();

public slots:
    void close();
    void connectButton_Click();
    void hostButton_Click();
    void synConnectionReady();

    friend class    Singleton<SynchDialog>;
};

#endif
