#pragma once

#include <memory>
#include <string>

#include <agentsdk/a2a/errors.hpp>
#include <agentsdk/a2a/request_response.hpp>
#include <agentsdk/a2a/result.hpp>
#include <agentsdk/a2a/types.hpp>

namespace agentsdk::a2a
{

/**
 * Callback interface for receiving streaming events.
 *
 * Implement this to process real-time task status and artifact updates.
 */
class stream_observer
{
public:
  virtual ~stream_observer () = default;

  /** Called when a streaming event is received. */
  virtual void on_event (const stream_response &event) = 0;

  /** Called when a stream error occurs. */
  virtual void on_error (const a2a_error &error) = 0;

  /** Called when the stream completes normally. */
  virtual void on_completed () = 0;
};

/**
 * Handle to an active stream.
 *
 * Allows cancellation and status checking from the calling thread.
 */
class stream_handle
{
public:
  virtual ~stream_handle () = default;

  /** Request cancellation of the stream. */
  virtual void cancel () = 0;

  /** Returns ``true`` while the stream is still active. */
  virtual bool is_active () const = 0;
};

/**
 * Abstract client transport.
 *
 * Concrete implementations (HTTP+JSON, JSON-RPC) implement this interface
 * to provide the actual network communication.
 */
class client_transport
{
public:
  virtual ~client_transport () = default;

  virtual result<send_message_response, a2a_error>
  send_message (const send_message_request &request) = 0;

  virtual result<task, a2a_error> get_task (const get_task_request &request)
      = 0;

  virtual result<list_tasks_response, a2a_error>
  list_tasks (const list_tasks_request &request) = 0;

  virtual result<task, a2a_error>
  cancel_task (const cancel_task_request &request) = 0;

  virtual std::unique_ptr<stream_handle>
  send_streaming_message (const send_message_request &request,
                          stream_observer &observer) = 0;

  virtual std::unique_ptr<stream_handle>
  subscribe_task (const std::string &task_id, stream_observer &observer) = 0;
};

/**
 * Abstract request handler.
 *
 * User code implements this interface to process A2A requests and
 * provide agent logic.
 */
class request_handler
{
public:
  virtual ~request_handler () = default;

  virtual result<send_message_response, a2a_error>
  on_send_message (const send_message_request &request) = 0;

  virtual result<task, a2a_error> on_get_task (const get_task_request &request)
      = 0;

  virtual result<list_tasks_response, a2a_error>
  on_list_tasks (const list_tasks_request &request) = 0;

  virtual result<task, a2a_error>
  on_cancel_task (const cancel_task_request &request) = 0;
};

} // namespace agentsdk::a2a
