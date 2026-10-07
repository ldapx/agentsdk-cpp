#pragma once

#include <cstdint>
#include <memory>
#include <thread>

#include <agentsdk/a2a/agent_card.hpp>
#include <agentsdk/a2a/transport.hpp>

namespace agentsdk::a2a
{

class http_json_server_transport;

/**
 * A2A server.
 *
 * Hosts an agent card and dispatches incoming requests to the
 * provided handler. The server binds a TCP port and serves one HTTP
 * request per connection, routing every request through
 * ``http_json_server_transport::dispatch`` (agent card discovery at
 * ``/.well-known/agent-card.json``, ``/message:send``,
 * ``/tasks/{id}``, ``:cancel``).
 *
 * Example::
 *
 *   a2a_server server (card, handler);
 *   server.serve (8080);      // blocking
 *   // or
 *   server.serve_async (8080); // background thread
 *   // ...
 *   server.stop ();
 */
class a2a_server
{
public:
  /**
   * Creates a server with the given agent card and request handler.
   *
   * :param card: The agent card served at ``/.well-known/agent-card.json``.
   * :param handler: The handler that processes A2A requests.
   */
  a2a_server (agent_card card, std::shared_ptr<request_handler> handler);

  ~a2a_server ();

  a2a_server (const a2a_server &) = delete;
  a2a_server &operator= (const a2a_server &) = delete;

  /**
   * Start serving on the given port. Blocks until ``stop()`` is
   * called from another thread.
   *
   * :param port: TCP port to listen on.
   */
  void serve (uint16_t port);

  /**
   * Start serving in a background thread. Calling it while a
   * background instance is running stops that instance first.
   *
   * :param port: TCP port to listen on.
   */
  void serve_async (uint16_t port);

  /** Signal the server to stop and join the background thread. */
  void stop ();

  /** Returns ``true`` while the server is running. */
  bool is_running () const;

private:
  std::unique_ptr<http_json_server_transport> m_transport;
  std::thread m_thread;
};

} // namespace agentsdk::a2a
