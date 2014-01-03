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

#include "gssh-types.h"

#define GSSH_TYPE_CONNECTION (gssh_connection_get_type ())
#define GSSH_CONNECTION(obj) (G_TYPE_CHECK_INSTANCE_CAST ((obj), GSSH_TYPE_CONNECTION, GSshConnection))

typedef struct _GSshConnection         GSshConnection;
typedef struct _GSshConnectionClass    GSshConnectionClass;

typedef enum /*< prefix=GSSH_CONNECTION_STATE >*/
  {
    GSSH_CONNECTION_STATE_DISCONNECTED,
    GSSH_CONNECTION_STATE_CONNECTING,
    GSSH_CONNECTION_STATE_HANDSHAKING,
    GSSH_CONNECTION_STATE_PREAUTH,
    GSSH_CONNECTION_STATE_NEGOTIATE_AUTH,
    GSSH_CONNECTION_STATE_AUTHENTICATION_REQUIRED,
    GSSH_CONNECTION_STATE_CONNECTED,
    GSSH_CONNECTION_STATE_ERROR
  } GSshConnectionState;

typedef enum {
  GSSH_CONNECTION_AUTH_MECHANISM_NONE,
  GSSH_CONNECTION_AUTH_MECHANISM_PASSWORD,
  GSSH_CONNECTION_AUTH_MECHANISM_PUBLICKEY,
  GSSH_CONNECTION_AUTH_MECHANISM_GSSAPI_MIC
} GSshConnectionAuthMechanism;

GType                   gssh_connection_get_type     (void);

GSshConnection       *gssh_connection_new (GSocketConnectable  *address,
                                               const char          *username);

const char * gssh_connection_auth_mechanism_to_string (GSshConnectionAuthMechanism  mech);

GSocketClient          *gssh_connection_get_socket_client (GSshConnection *self);

GSshConnectionState   gssh_connection_get_state (GSshConnection        *self);

void                    gssh_connection_reset (GSshConnection      *self);

void                    gssh_connection_handshake_async (GSshConnection    *self,
                                                         GCancellable        *cancellable,
                                                         GAsyncReadyCallback  callback,
                                                         gpointer             user_data);

gboolean                gssh_connection_handshake_finish (GSshConnection    *self,
                                                          GAsyncResult        *result,
                                                          GError             **error);

void                    gssh_connection_set_interaction (GSshConnection   *self,
                                                         GTlsInteraction  *interaction);

void                    gssh_connection_preauth_get_host_key (GSshConnection   *self,
                                                              char            **out_keytype,
                                                              char            **out_key_sha1_text,
                                                              char            **out_key_base64);

void                    gssh_connection_negotiate_async (GSshConnection      *self,
                                                         GCancellable        *cancellable,
                                                         GAsyncReadyCallback  callback,
                                                         gpointer             user_data);

gboolean                gssh_connection_negotiate_finish (GSshConnection      *self,
                                                          GAsyncResult        *res,
                                                          GError             **error);

void                    gssh_connection_get_authentication_mechanisms (GSshConnection              *self,
                                                                       guint                      **out_authmechanisms,
                                                                       guint                       *out_len);

void                    gssh_connection_auth_async (GSshConnection               *self,
                                                    GSshConnectionAuthMechanism   authmech,
                                                    GCancellable                 *cancellable,
                                                    GAsyncReadyCallback           callback,
                                                    gpointer                      user_data);

gboolean                gssh_connection_auth_finish (GSshConnection      *self,
                                                     GAsyncResult        *result,
                                                     GError             **error);

void                    gssh_connection_open_shell_async (GSshConnection         *self,
                                                            GCancellable             *cancellable,
                                                            GAsyncReadyCallback       callback,
                                                            gpointer                  user_data);

GSshChannel *         gssh_connection_open_shell_finish (GSshConnection         *self,
                                                             GAsyncResult             *result,
                                                             GError                  **error);

void                    gssh_connection_exec_async (GSshConnection           *self,
                                                    const char               *shell_string,
                                                    GCancellable             *cancellable,
                                                    GAsyncReadyCallback       callback,
                                                    gpointer                  user_data);

GSshChannel *         gssh_connection_exec_finish (GSshConnection         *self,
                                                   GAsyncResult             *result,
                                                   GError                  **error);

