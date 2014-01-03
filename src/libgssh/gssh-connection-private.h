/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*-
 *
 * Copyright (C) 2013 Colin Walters <walters@verbum.org>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#pragma once

#include "gssh-connection.h"
#include <libssh/libssh.h>

struct _GSshConnection
{
  GObject parent;

  GSshConnectionState state;

  guint paused : 1;
  guint select_inbound : 1;
  guint select_outbound : 1;
  guint preauth_continue : 1;
  guint tried_userauth_none : 1;
  guint unused : 27;

  char *username;

  GTlsInteraction *interaction;

  GArray *authmechanisms;

  ssh_session session;
  GHashTable *channels;

  GError *cached_error;
  char *remote_hostkey_type;
  char *remote_hostkey_sha1_text;
  char *remote_hostkey_base64;
  GMainContext *maincontext;
  GSocketClient *socket_client;
  GSocket *socket;
  GSource *socket_source;
  GSocketConnectable *address;
  GSocketConnection *socketconn;
  GCancellable *cancellable;
  char *password;

  GTask *handshake_task;
  GSshConnectionAuthMechanism current_authmech;
  GTask *auth_task;
  GTask *negotiate_task;
  GHashTable *open_channel_exec_tasks;
  GHashTable *channel_tasks;
};

struct _GSshConnectionClass
{
  GObjectClass parent_class;
};

typedef struct _GSshConnectionPrivate GSshConnectionPrivate;

void
_gssh_set_error_from_libssh (GError         **error,
                             const char      *prefix,
                             ssh_session      session);
