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

#include "gssh-channel-output-stream.h"
#include "gssh-channel-private.h"
#include "gssh-connection-private.h"

typedef enum {
  GSSH_CHANNEL_OUTPUT_STREAM_STATE_OPEN,
  GSSH_CHANNEL_OUTPUT_STREAM_STATE_REQUESTED_EOF,
  GSSH_CHANNEL_OUTPUT_STREAM_STATE_SENT_EOF,
  GSSH_CHANNEL_OUTPUT_STREAM_STATE_RECEIVED_EOF
} GSshChannelOutputStreamState;

struct _GSshChannelOutputStream
{
  GOutputStream parent;
  GSshChannel *channel;
 
  GSshChannelOutputStreamState state;

  int efd;
  GBytes *buf;

  GTask *write_task;
  GTask *close_task;

  /* For synchronous operations */
  gboolean sync_running;
  gssize sync_bytes_written;
  GMainContext *sync_context;
  GError *sync_error;
};

struct _GSshChannelOutputStreamClass
{
  GOutputStreamClass parent;
};

G_DEFINE_TYPE(GSshChannelOutputStream, _gssh_channel_output_stream, G_TYPE_OUTPUT_STREAM);

static void
_gssh_channel_output_stream_init (GSshChannelOutputStream *self)
{
}

static void
_gssh_channel_output_stream_dispose (GObject *object)
{
  G_GNUC_UNUSED GSshChannelOutputStream *self = (GSshChannelOutputStream*)object;

  G_OBJECT_CLASS (_gssh_channel_output_stream_parent_class)->dispose (object);
}

static void
gssh_channel_output_stream_close_async (GOutputStream       *stream,
                                          int                  io_priority,
                                          GCancellable        *cancellable,
                                          GAsyncReadyCallback  callback,
                                          gpointer             data)
{
  GSshChannelOutputStream *self = GSSH_CHANNEL_OUTPUT_STREAM (stream);
  GTask *task;

  g_return_if_fail (self->state == GSSH_CHANNEL_OUTPUT_STREAM_STATE_OPEN);

  self->state = GSSH_CHANNEL_OUTPUT_STREAM_STATE_REQUESTED_EOF;

  task = g_task_new (self, cancellable, callback, data);
  g_task_set_priority (task, io_priority);

  self->close_task = task;

  _gssh_channel_output_stream_iteration (self);
}

static gboolean
gssh_channel_output_stream_close_finish (GOutputStream  *stream,
                                           GAsyncResult   *result,
                                           GError        **error)
{
  g_return_val_if_fail (g_task_is_valid (result, stream), FALSE);

  return g_task_propagate_boolean (G_TASK (result), error);
}

static void
on_sync_close_done (GObject            *src,
                    GAsyncResult       *result,
                    gpointer            user_data)
{
  GSshChannelOutputStream *self = GSSH_CHANNEL_OUTPUT_STREAM (user_data);

  (void) gssh_channel_output_stream_close_finish ((GOutputStream*)self, result, &self->sync_error);
  self->sync_running = FALSE;
  g_main_context_wakeup (self->sync_context);
}

static gboolean
gssh_channel_output_stream_close (GOutputStream  *stream,
                                    GCancellable   *cancellable,
                                    GError        **error)
{
  GSshChannelOutputStream *self = GSSH_CHANNEL_OUTPUT_STREAM (stream);

  self->sync_context = g_main_context_new ();
  g_main_context_push_thread_default (self->sync_context);
  self->sync_error = NULL;
  self->sync_running = FALSE;

  gssh_channel_output_stream_close_async (stream, G_PRIORITY_DEFAULT, cancellable,
                                            on_sync_close_done, self);

  while (self->sync_running)
    g_main_context_iteration (self->sync_context, TRUE);

  g_main_context_pop_thread_default (self->sync_context);

  if (self->sync_error)
    {
      g_propagate_error (error, self->sync_error);
      return FALSE;
    }
  return TRUE;
}

static void
gssh_channel_output_stream_write_async (GOutputStream       *stream,
                                        const void          *buffer,
                                        gsize                count,
                                        int                  io_priority,
                                        GCancellable        *cancellable,
                                        GAsyncReadyCallback  callback,
                                        gpointer             user_data)
{
  GSshChannelOutputStream *self = GSSH_CHANNEL_OUTPUT_STREAM (stream);

  g_return_if_fail (self->state == GSSH_CHANNEL_OUTPUT_STREAM_STATE_OPEN);

  g_assert (self->write_task == NULL);
  self->write_task = g_task_new (self, cancellable, callback, user_data);
  g_task_set_priority (self->write_task, io_priority);

  /* Not really static, but the caller has to keep buffer alive. */
  self->buf = g_bytes_new_static (buffer, count);

  _gssh_channel_output_stream_iteration (self);
}

static gssize
gssh_channel_output_stream_write_finish (GOutputStream  *stream,
                                                GAsyncResult   *result,
                                                GError        **error)
{
  g_return_val_if_fail (g_task_is_valid (result, stream), FALSE);

  return g_task_propagate_int (G_TASK (result), error);
}

static void
on_sync_write_done (GObject            *src,
                    GAsyncResult       *result,
                    gpointer            user_data)
{
  GSshChannelOutputStream *self = GSSH_CHANNEL_OUTPUT_STREAM (user_data);

  self->sync_bytes_written = gssh_channel_output_stream_write_finish ((GOutputStream*)self, result, &self->sync_error);
  self->sync_running = FALSE;
  g_main_context_wakeup (self->sync_context);
}

static gssize
gssh_channel_output_stream_write (GOutputStream  *stream,
                                    const void     *buffer,
                                    gsize           count,
                                    GCancellable   *cancellable,
                                    GError        **error)
{
  GSshChannelOutputStream *self = GSSH_CHANNEL_OUTPUT_STREAM (stream);

  g_return_val_if_fail (self->state == GSSH_CHANNEL_OUTPUT_STREAM_STATE_OPEN, -1);

  self->sync_context = g_main_context_new ();
  g_main_context_push_thread_default (self->sync_context);
  self->sync_error = NULL;
  self->sync_running = FALSE;

  gssh_channel_output_stream_write_async (stream, buffer, count,
                                                 G_PRIORITY_DEFAULT, cancellable,
                                                 on_sync_write_done, self);

  while (self->sync_running)
    g_main_context_iteration (self->sync_context, TRUE);

  g_main_context_pop_thread_default (self->sync_context);

  if (self->sync_error)
    {
      g_propagate_error (error, self->sync_error);
      return -1;
    }
  return self->sync_bytes_written;
}

void
_gssh_channel_output_stream_iteration (GSshChannelOutputStream     *self)
{
  int rc;
  GError *local_error = NULL;
  GTask *task;

  task = self->write_task ? self->write_task : self->close_task;

  if (!task)
    return;

  if (!self->channel->connection)
    {
      g_set_error_literal (&local_error, G_IO_ERROR, G_IO_ERROR_CLOSED,
                           "Connection closed");
      if (self->write_task)
        self->write_task = NULL;
      else
        self->close_task = NULL;
      g_task_return_error (task, local_error);
      g_object_unref (task);
      return;
    }
  
  switch (self->state)
    {
    case GSSH_CHANNEL_OUTPUT_STREAM_STATE_OPEN:
      {
        gsize bufsize;
        const guint8 *data;

        data = g_bytes_get_data (self->buf, &bufsize);
        rc = ssh_channel_write (self->channel->libsshchannel,
                                (const char*)data, bufsize);
        if (rc == 0)
          break;
        
        /* This special dance is required because we may have
           reentered via g_task_return() */
        self->write_task = NULL;

        if (rc > 0)
          {
            g_task_return_int (task, rc);
          }
        else
          {
            _gssh_set_error_from_libssh (&local_error, "Failed to write",
                                         self->channel->connection->session);
            g_task_return_error (task, local_error);
          }
      }
      break;
    case GSSH_CHANNEL_OUTPUT_STREAM_STATE_REQUESTED_EOF:
      {
        rc = ssh_channel_send_eof (self->channel->libsshchannel);
        if (rc == SSH_AGAIN)
          break;
        else if (rc == SSH_OK)
          {
            self->state = GSSH_CHANNEL_OUTPUT_STREAM_STATE_SENT_EOF;
          }
        else
          {
            _gssh_set_error_from_libssh (&local_error, "Failed to close",
                                         self->channel->connection->session);
            self->close_task = NULL;
            g_task_return_error (task, local_error);
            g_object_unref (task);
            break;
          }
      }
      /* Fall though */
    case GSSH_CHANNEL_OUTPUT_STREAM_STATE_SENT_EOF:
      rc = ssh_channel_is_eof (self->channel->libsshchannel);
      if (rc == 1)
        {
          self->close_task = NULL;
          self->state = GSSH_CHANNEL_OUTPUT_STREAM_STATE_RECEIVED_EOF;
          g_task_return_boolean (task, TRUE);
          g_object_unref (task);
        }
      else if (rc == 0)
        {
          break;
        }
      else
        g_assert_not_reached ();
      /* Fall through */
    case GSSH_CHANNEL_OUTPUT_STREAM_STATE_RECEIVED_EOF:
      /* Nothing */
      break;
    }
}

GSshChannelOutputStream  *
_gssh_channel_output_stream_new (GSshChannel *channel)
{
  GSshChannelOutputStream *ret = g_object_new (GSSH_TYPE_CHANNEL_OUTPUT_STREAM, NULL);
  ret->channel = channel;
  return ret;
}

static void
_gssh_channel_output_stream_class_init (GSshChannelOutputStreamClass *class)
{
  GOutputStreamClass *ostream_class;

  G_OBJECT_CLASS (class)->dispose = _gssh_channel_output_stream_dispose;

  ostream_class = G_OUTPUT_STREAM_CLASS (class);
  ostream_class->write_fn = gssh_channel_output_stream_write;
  ostream_class->write_async = gssh_channel_output_stream_write_async;
  ostream_class->write_finish = gssh_channel_output_stream_write_finish;
  ostream_class->close_fn = gssh_channel_output_stream_close;
  ostream_class->close_async  = gssh_channel_output_stream_close_async;
  ostream_class->close_finish = gssh_channel_output_stream_close_finish;
}
