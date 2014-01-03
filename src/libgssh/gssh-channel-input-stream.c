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

#include "gssh-channel-input-stream.h"
#include "gssh-connection-private.h"
#include "gssh-channel-private.h"

struct _GSshChannelInputStream
{
  GInputStream parent;

  GSshChannel *channel;
 
  gboolean is_closed;

  GTask *read_task;
  GTask *close_task;

  void *buf;
  gsize count;

  /* For synchronous operations */
  gboolean sync_running;
  gssize sync_bytes_read;
  GMainContext *sync_context;
  GError *sync_error;
};

struct _GSshChannelInputStreamClass
{
  GInputStreamClass parent;
};

G_DEFINE_TYPE(GSshChannelInputStream, _gssh_channel_input_stream, G_TYPE_INPUT_STREAM);

static void
_gssh_channel_input_stream_init (GSshChannelInputStream *self)
{
}

static void
_gssh_channel_input_stream_dispose (GObject *object)
{
  G_GNUC_UNUSED GSshChannelInputStream *self = (GSshChannelInputStream*)object;

  G_OBJECT_CLASS (_gssh_channel_input_stream_parent_class)->dispose (object);
}

static gboolean
gssh_channel_input_stream_close (GInputStream  *stream,
                                    GCancellable   *cancellable,
                                    GError        **error)
{
  GSshChannelInputStream *self = GSSH_CHANNEL_INPUT_STREAM (stream);

  g_return_val_if_fail (!self->is_closed, FALSE);

  self->is_closed = TRUE;

  return TRUE;
}

static void
gssh_channel_input_stream_close_async (GInputStream        *stream,
                                         int                  io_priority,
                                         GCancellable        *cancellable,
                                         GAsyncReadyCallback  callback,
                                         gpointer             data)
{
  GSshChannelInputStream *self = GSSH_CHANNEL_INPUT_STREAM (stream);
  GTask *task;

  g_return_if_fail (!self->is_closed);

  task = g_task_new (stream, cancellable, callback, data);
  gssh_channel_input_stream_close (stream, cancellable, NULL);
  g_task_return_boolean (task, TRUE);
  g_object_unref (task);
}

static gboolean
gssh_channel_input_stream_close_finish (GInputStream  *stream,
                                           GAsyncResult   *result,
                                           GError        **error)
{
  g_return_val_if_fail (g_task_is_valid (result, stream), FALSE);

  return g_task_propagate_boolean (G_TASK (result), error);
}

static void
gssh_channel_input_stream_read_async (GInputStream        *stream,
                                        void                *buffer,
                                        gsize                count,
                                        int                  io_priority,
                                        GCancellable        *cancellable,
                                        GAsyncReadyCallback  callback,
                                        gpointer             user_data)
{
  GSshChannelInputStream *self = GSSH_CHANNEL_INPUT_STREAM (stream);

  g_return_if_fail (!self->is_closed);

  g_assert (self->read_task == NULL);
  self->read_task = g_task_new (self, cancellable, callback, user_data);
  g_task_set_priority (self->read_task, io_priority);

  self->buf = buffer;
  self->count = count;

  _gssh_channel_input_stream_iteration (self);
}

static gssize
gssh_channel_input_stream_read_finish (GInputStream  *stream,
                                         GAsyncResult   *result,
                                         GError        **error)
{
  g_return_val_if_fail (g_task_is_valid (result, stream), FALSE);

  return g_task_propagate_int (G_TASK (result), error);
}

static void
on_sync_read_done (GObject            *src,
                   GAsyncResult       *result,
                   gpointer            user_data)
{
  GSshChannelInputStream *self = GSSH_CHANNEL_INPUT_STREAM (user_data);

  self->sync_bytes_read = gssh_channel_input_stream_read_finish ((GInputStream*)self, result, &self->sync_error);
  self->sync_running = FALSE;
  g_main_context_wakeup (self->sync_context);
}

static gssize
gssh_channel_input_stream_read (GInputStream   *stream,
                                  void           *buffer,
                                  gsize           count,
                                  GCancellable   *cancellable,
                                  GError        **error)
{
  GSshChannelInputStream *self = GSSH_CHANNEL_INPUT_STREAM (stream);

  g_return_val_if_fail (!self->is_closed, -1);

  self->sync_context = g_main_context_new ();
  g_main_context_push_thread_default (self->sync_context);
  self->sync_error = NULL;
  self->sync_running = TRUE;

  gssh_channel_input_stream_read_async (stream, buffer, count,
                                          G_PRIORITY_DEFAULT, cancellable,
                                          on_sync_read_done, self);

  while (self->sync_running)
    g_main_context_iteration (self->sync_context, TRUE);

  g_main_context_pop_thread_default (self->sync_context);

  if (self->sync_error)
    {
      g_propagate_error (error, self->sync_error);
      return -1;
    }
  return self->sync_bytes_read;
}

void
_gssh_channel_input_stream_iteration (GSshChannelInputStream     *self)
{
  int rc;
  GError *local_error = NULL;
  GTask *prev_task = self->read_task;
  gboolean is_eof = FALSE;

  if (!prev_task)
    return;

  g_assert (!self->is_closed);
  rc = ssh_channel_read_nonblocking (self->channel->libsshchannel,
                                     self->buf, self->count, 0);
  if (rc == 0)
    {
      is_eof = ssh_channel_is_eof (self->channel->libsshchannel);
      if (!is_eof)
        {
          /* We'll get 0 but !is_eof when we hit the libssh equivalent
           * of EAGAIN.  We need to wait for poll().
           */
          return;
        }
    }

  /* This special dance is required because we may have reentered via
     g_task_return() */
  self->read_task = NULL;

  if (is_eof)
    {
      g_debug ("channel eof");
      g_task_return_int (prev_task, 0);
    }
  else if (rc > 0)
    {
      g_debug ("channel read %" G_GSSIZE_FORMAT " bytes", (gssize)rc);
      g_task_return_int (prev_task, rc);
    }
  else
    {
      _gssh_set_error_from_libssh (&local_error, "Failed to read",
                                   self->channel->connection->session);
      g_task_return_error (prev_task, local_error);
    }
  g_object_unref (prev_task);
}

GSshChannelInputStream  *
_gssh_channel_input_stream_new (GSshChannel *channel)
{
  GSshChannelInputStream *ret = g_object_new (GSSH_TYPE_CHANNEL_INPUT_STREAM, NULL);
  ret->channel = channel;
  return ret;
}

static void
_gssh_channel_input_stream_class_init (GSshChannelInputStreamClass *class)
{
  GInputStreamClass *istream_class;

  G_OBJECT_CLASS (class)->dispose = _gssh_channel_input_stream_dispose;

  istream_class = G_INPUT_STREAM_CLASS (class);
  istream_class->read_fn = gssh_channel_input_stream_read;
  istream_class->read_async = gssh_channel_input_stream_read_async;
  istream_class->read_finish = gssh_channel_input_stream_read_finish;
  istream_class->close_fn = gssh_channel_input_stream_close;
  istream_class->close_async  = gssh_channel_input_stream_close_async;
  istream_class->close_finish = gssh_channel_input_stream_close_finish;
}
