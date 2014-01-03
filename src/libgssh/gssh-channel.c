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

#include "gssh-channel-private.h"
#include <glib-unix.h>
#include <fcntl.h>

#include "libgsystem.h"

G_DEFINE_TYPE(GSshChannel, gssh_channel, G_TYPE_IO_STREAM);

static GInputStream *
gssh_channel_get_input_stream (GIOStream *io_stream)
{
  GSshChannel *self = GSSH_CHANNEL (io_stream);

  if (self->input_stream == NULL)
    self->input_stream = _gssh_channel_input_stream_new (self);

  return (GInputStream*)self->input_stream;
}

static GOutputStream *
gssh_channel_get_output_stream (GIOStream *io_stream)
{
  GSshChannel *self = GSSH_CHANNEL (io_stream);

  if (self->output_stream == NULL)
    self->output_stream = _gssh_channel_output_stream_new (self);

  return (GOutputStream*)self->output_stream;
}

static gboolean
gssh_channel_close (GIOStream     *stream,
                      GCancellable  *cancellable,
                      GError       **error)
{
  GSshChannel *self = (GSshChannel*)stream;

  g_return_val_if_fail (self->connection->state == GSSH_CONNECTION_STATE_CONNECTED, FALSE);
  
  if (self->connection)
    g_hash_table_remove (self->connection->channels, self);

  return G_IO_STREAM_CLASS (gssh_channel_parent_class)->close_fn (stream, cancellable, error);
}

static void
gssh_channel_close_async (GIOStream           *stream,
                            int                  io_priority,
                            GCancellable        *cancellable,
                            GAsyncReadyCallback  callback,
                            gpointer             user_data)
{
  GSshChannel *self = (GSshChannel*)stream;
  
  if (self->connection)
    g_hash_table_remove (self->connection->channels, self);

  G_IO_STREAM_CLASS (gssh_channel_parent_class)->close_async (stream, io_priority,
                                                                cancellable, callback,
                                                                user_data);
}

static gboolean
gssh_channel_close_finish (GIOStream     *stream,
                             GAsyncResult  *result,
                             GError       **error)
{
  return G_IO_STREAM_CLASS (gssh_channel_parent_class)->close_finish (stream, result, error);
}

void
_gssh_channel_iteration (GSshChannel              *self)
{
  int rc;
  GError *local_error = NULL;

  if (self->pty_size_task)
    {
      GTask *orig_pty_size_task = self->pty_size_task;
      rc = ssh_channel_change_pty_size (self->libsshchannel,
                                        self->pty_width, self->pty_height);
      if (rc == SSH_AGAIN)
        ;
      else 
        {
          self->pty_size_task = NULL;
          if (rc < 0)
            {
              _gssh_set_error_from_libssh (&local_error, "Failed to set pty size",
                                           self->connection->session);
              g_task_return_error (orig_pty_size_task, local_error);
            }
          else
            {
              g_assert (rc == 0);
              g_task_return_boolean (orig_pty_size_task, TRUE);
            }
          g_object_unref (orig_pty_size_task);
        }
    }
}

void
gssh_channel_request_pty_size_async (GSshChannel         *self,
                                     guint                width,
                                     guint                height,
                                     GCancellable        *cancellable,
                                     GAsyncReadyCallback  callback,
                                     gpointer             user_data)
{
  g_return_if_fail (self->have_pty);
  g_return_if_fail (self->pty_size_task == NULL);
  g_return_if_fail (self->connection->state == GSSH_CONNECTION_STATE_CONNECTED);

  self->pty_size_task = g_task_new (self, cancellable, callback, user_data);
  self->pty_width = width;
  self->pty_height = height;

  _gssh_channel_iteration (self);
}

gboolean
gssh_channel_request_pty_size_finish (GSshChannel         *self,
                                      GAsyncResult        *res,
                                      GError             **error)
{
  g_return_val_if_fail (g_task_is_valid (res, self), FALSE);
  return g_task_propagate_boolean (G_TASK (res), error);
}

int
gssh_channel_get_exit_code (GSshChannel *self)
{
  return ssh_channel_get_exit_status (self->libsshchannel);
}

static void
gssh_channel_init (GSshChannel *self)
{
}

static void
gssh_channel_dispose (GObject *object)
{
  GSshChannel *self = (GSshChannel*)object;

  g_clear_object (&self->input_stream);
  g_clear_object (&self->output_stream);

  G_OBJECT_CLASS (gssh_channel_parent_class)->dispose (object);
}

static void
gssh_channel_class_init (GSshChannelClass *class)
{
  G_OBJECT_CLASS (class)->dispose = gssh_channel_dispose;
  G_IO_STREAM_CLASS (class)->get_input_stream = gssh_channel_get_input_stream;
  G_IO_STREAM_CLASS (class)->get_output_stream = gssh_channel_get_output_stream;
  G_IO_STREAM_CLASS (class)->close_fn = gssh_channel_close;
  G_IO_STREAM_CLASS (class)->close_async = gssh_channel_close_async;
  G_IO_STREAM_CLASS (class)->close_finish = gssh_channel_close_finish;
}

GSshChannel *
_gssh_channel_new (GSshConnection   *connection,
                   gboolean         have_pty,
                   ssh_channel      libsshchannel)
{
  GSshChannel *self = (GSshChannel*)g_object_new (GSSH_TYPE_CHANNEL, NULL);
  /* We don't hold a ref; if the connection goes away, it will ensure
   * this pointer is zeroed.
   */
  self->connection = connection; 
  self->have_pty = have_pty; 
  self->libsshchannel = libsshchannel; 
  return self;
}
