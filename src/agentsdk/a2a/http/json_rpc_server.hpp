#pragma once

#include <atomic>
#include <memory>
#include <string>

#include <agentsdk/a2a/agent_card.hpp>
#include <agentsdk/a2a/transport.hpp>

namespace agentsdk::a2a
{

/**
 * JSON-RPC 2.0 server transport.
 *
 * Serves A2A requests over JSON-RPC 2.0 at the ``/rpc`` endpoint.
 * Supports all A2A methods with standard JSON-RPC request/response framing.
 */
class json_rpc_server_transport
{
public:
  /**
   * Creates a JSON-RPC server transport.
   *
   * :param card: Agent card served at ``/.well-known/agent-card.json``.
   * :param handler: Request handler for A2A operations.
   */
  json_rpc_server_transport (agent_card card,
                             std::shared_ptr<request_handler> handler);

  ~json_rpc_server_transport ();

  json_rpc_server_transport (const json_rpc_server_transport &) = delete;
  json_rpc_server_transport &operator= (const json_rpc_server_transport &)
      = delete;

  /**
   * Start serving on the given port. Blocks until ``stop()`` is called.
   *
   * :param port: TCP port to listen on.
   */
  void serve (uint16_t port);

  /** Signal the server to stop. Thread-safe. */
  void stop ();

  /** Returns ``true`` while the server is running. */
  bool is_running () const;

private:
  /**
   * Dispatch a JSON-RPC request and produce a response string.
   *
   * :param request_body: The raw JSON-RPC request body.
   * :return: The JSON-RPC response body.
   */
  std::string dispatch_rpc (const std::string &request_body);

  agent_card m_card;
  std::shared_ptr<request_handler> m_handler;
  std::atomic<bool> m_running{ false };
};

} // namespace agentsdk::a2a
