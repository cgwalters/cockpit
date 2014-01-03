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

#define GSSH_TYPE_CHANNEL (gssh_channel_get_type ())
#define GSSH_CHANNEL(obj) (G_TYPE_CHECK_INSTANCE_CAST ((obj), GSSH_TYPE_CHANNEL, GSshChannel))

typedef struct _GSshChannelClass    GSshChannelClass;

GType                   gssh_channel_get_type     (void);

void gssh_channel_request_pty_size_async (GSshChannel         *self,
                                          guint                width,
                                          guint                height,
                                          GCancellable        *cancellable,
                                          GAsyncReadyCallback  callback,
                                          gpointer             user_data);

gboolean gssh_channel_request_pty_size_finish (GSshChannel         *self,
                                               GAsyncResult        *res,
                                               GError             **error);

int gssh_channel_get_exit_code (GSshChannel *self);
