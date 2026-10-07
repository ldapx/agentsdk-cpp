#pragma once

#include <atomic>
#include <memory>
#include <string>

#include <agentsdk/a2a/agent_card.hpp>
#include <agentsdk/a2a/transport.hpp>

namespace agentsdk::a2a
{

/**
 * HTTP+JSON server transport.
 *
 * Serves A2A requests over plain HTTP with JSON request/response bodies.
 * Routes requests based on URL path to the appropriate handler method.
 */
class http_json_server_transport
{
public:
  /**
   * Creates a server transport with the given agent card and handler.
   *
   * :param card: Agent card served at ``/.well-known/agent-card.json``.
   * :param handler: Request handler for A2A operations.
   */
  http_json_server_transport (agent_card card,
                              std::shared_ptr<request_handler> handler);

  ~http_json_server_transport ();

  http_json_server_transport (const http_json_server_transport &) = delete;
  http_json_server_transport &operator= (const http_json_server_transport &)
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
   * Dispatch an incoming HTTP request and produce a response.
   *
   * :param method: HTTP method (GET, POST, DELETE).
   * :param path: URL path.
   * :param body: Request body (for POST).
   * :return: Response body string.
   */
  std::string dispatch (const std::string &method, const std::string &path,
                        const std::string &body);

  agent_card m_card;
  std::shared_ptr<request_handler> m_handler;
  std::atomic<bool> m_running{ false };
};

} // namespace agentsdk::a2a
