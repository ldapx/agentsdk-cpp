// Part of agentsdk_core_tests; the doctest main lives in
// test_main.cpp.
#include "doctest.h"

#include "agentsdk/a2a/errors.hpp"
#include "agentsdk/a2a/request_response.hpp"
#include "agentsdk/a2a/server.hpp"
#include "agentsdk/a2a/transport.hpp"

#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <chrono>
#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <thread>

using namespace agentsdk;
using namespace agentsdk::a2a;

namespace
{

/**
 * Handler that rejects everything: the smoke test only exercises the
 * agent-card endpoint, so no handler method may be reached.
 */
class stub_handler : public request_handler
{
public:
  result<send_message_response, a2a_error>
  on_send_message (const send_message_request &) override
  {
    return a2a_error{ error_code::method_not_found, "unimplemented" };
  }

  result<task, a2a_error>
  on_get_task (const get_task_request &) override
  {
    return a2a_error{ error_code::method_not_found, "unimplemented" };
  }

  result<list_tasks_response, a2a_error>
  on_list_tasks (const list_tasks_request &) override
  {
    return a2a_error{ error_code::method_not_found, "unimplemented" };
  }

  result<task, a2a_error>
  on_cancel_task (const cancel_task_request &) override
  {
    return a2a_error{ error_code::method_not_found, "unimplemented" };
  }
};

/** Ask the kernel for a free TCP port (bind :0, read it back, close). */
uint16_t
find_free_port ()
{
  const int fd = socket (AF_INET, SOCK_STREAM, 0);
  if (fd < 0) {
    return 0;
  }
  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl (INADDR_LOOPBACK);
  addr.sin_port = 0;
  uint16_t port = 0;
  if (bind (fd, reinterpret_cast<sockaddr *> (&addr), sizeof (addr)) == 0) {
    socklen_t len = sizeof (addr);
    if (getsockname (fd, reinterpret_cast<sockaddr *> (&addr), &len) == 0) {
      port = ntohs (addr.sin_port);
    }
  }
  close (fd);
  return port;
}

/** Minimal blocking HTTP GET; returns the full response or "". */
std::string
http_get (uint16_t port, const std::string &path)
{
  const int fd = socket (AF_INET, SOCK_STREAM, 0);
  if (fd < 0) {
    return {};
  }
  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_port = htons (port);
  addr.sin_addr.s_addr = htonl (INADDR_LOOPBACK);
  if (connect (fd, reinterpret_cast<sockaddr *> (&addr), sizeof (addr)) < 0) {
    close (fd);
    return {};
  }

  // 2 s connect/read budget so a broken server fails the test fast.
  timeval timeout{};
  timeout.tv_sec = 2;
  setsockopt (fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof (timeout));

  const std::string request
      = "GET " + path + " HTTP/1.1\r\nHost: 127.0.0.1\r\n\r\n";
  if (send (fd, request.data (), request.size (), 0)
      != static_cast<ssize_t> (request.size ())) {
    close (fd);
    return {};
  }

  std::string response;
  char buf[4096];
  ssize_t n = 0;
  while ((n = recv (fd, buf, sizeof (buf), 0)) > 0) {
    response.append (buf, static_cast<std::size_t> (n));
  }
  close (fd);
  return response;
}

} // namespace

// Regression test for the "A2A server never listens" finding: serve()
// used to only flip an atomic and sleep. It must now bind a real
// socket, answer the discovery endpoint, and shut down on stop().
TEST_CASE ("a2a_server binds a socket and serves the agent card")
{
  agent_card card;
  card.name = "Smoke Test Agent";
  card.description = "serves itself for the refactoring smoke test";
  card.version = "0.0.1";

  a2a_server server (card, std::make_shared<stub_handler> ());
  REQUIRE_FALSE (server.is_running ());

  const uint16_t port = find_free_port ();
  REQUIRE (port != 0);

  server.serve_async (port);

  // Retry until the background thread has entered the accept loop.
  std::string response;
  for (int attempt = 0; attempt < 50 && response.empty (); ++attempt) {
    std::this_thread::sleep_for (std::chrono::milliseconds{ 20 });
    response = http_get (port, "/.well-known/agent-card.json");
  }

  REQUIRE (response.empty () == false);
  CHECK (response.find ("HTTP/1.1 200 OK") != std::string::npos);
  CHECK (response.find ("Content-Type: application/json") != std::string::npos);
  CHECK (response.find ("Smoke Test Agent") != std::string::npos);

  server.stop ();
  CHECK_FALSE (server.is_running ());
}
