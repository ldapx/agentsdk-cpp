#pragma once

#include <memory>

#include <agentsdk/a2a/errors.hpp>
#include <agentsdk/a2a/request_response.hpp>
#include <agentsdk/a2a/transport.hpp>
#include <agentsdk/a2a/types.hpp>

namespace agentsdk::a2a
{

/**
 * Transport-agnostic A2A client.
 *
 * Delegates all network operations to a pluggable ``client_transport``.
 *
 * Example::
 *
 *   auto transport = std::make_unique<http_json_client_transport> (iface);
 *   a2a_client client (std::move (transport));
 *   auto result = client.send_message (req);
 */
class a2a_client
{
public:
  explicit a2a_client (std::unique_ptr<client_transport> transport);

  /** Send a message and get back a task or direct response. */
  result<send_message_response, a2a_error>
  send_message (const send_message_request &request);

  /** Get the current state of a task. */
  result<task, a2a_error> get_task (const std::string &task_id,
                                           std::optional<int32_t> history_length
                                           = std::nullopt);

  /** List tasks with optional filtering. */
  result<list_tasks_response, a2a_error>
  list_tasks (const list_tasks_request &request);

  /** Cancel a running task. */
  result<task, a2a_error> cancel_task (const std::string &task_id);

  /** Send a message and stream updates in real-time. */
  std::unique_ptr<stream_handle>
  send_streaming_message (const send_message_request &request,
                          stream_observer &observer);

  /** Subscribe to updates for an existing task. */
  std::unique_ptr<stream_handle> subscribe_task (const std::string &task_id,
                                                 stream_observer &observer);

private:
  std::unique_ptr<client_transport> m_transport;
};

} // namespace agentsdk::a2a
