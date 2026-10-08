#pragma once

#include <string>

#include <agentsdk/a2a/agent_card.hpp>
#include <agentsdk/a2a/transport.hpp>

extern "C" {
#include <curl/curl.h>
}

namespace agentsdk::a2a
{

/**
 * HTTP+JSON client transport.
 *
 * Sends A2A requests as plain HTTP POST with JSON bodies.
 * Supports SSE streaming via ``curl_multi`` non-blocking I/O.
 */
class http_json_client_transport : public client_transport
{
public:
  /**
   * Creates a transport targeting the given agent interface.
   *
   * :param iface: The transport endpoint from an agent card.
   */
  explicit http_json_client_transport (const agent_interface &iface);

  ~http_json_client_transport () override;

  result<send_message_response, a2a_error>
  send_message (const send_message_request &request) override;

  result<task, a2a_error> get_task (const get_task_request &request) override;

  result<list_tasks_response, a2a_error>
  list_tasks (const list_tasks_request &request) override;

  result<task, a2a_error>
  cancel_task (const cancel_task_request &request) override;

  std::unique_ptr<stream_handle>
  send_streaming_message (const send_message_request &request,
                          stream_observer &observer) override;

  std::unique_ptr<stream_handle>
  subscribe_task (const std::string &task_id,
                  stream_observer &observer) override;

private:
  /** Perform a blocking POST request and return the response body. */
  result<std::string, a2a_error> post (const std::string &path,
                                       const std::string &body);

  /** Perform a blocking GET request and return the response body. */
  result<std::string, a2a_error> get (const std::string &path);

  std::string m_base_url;
  std::string m_tenant;
};

} // namespace agentsdk::a2a
