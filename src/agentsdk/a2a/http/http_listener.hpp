#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <string>

namespace agentsdk::a2a::http
{

/**
 * Routes one parsed HTTP request to its response body.
 *
 * Parameters: (method, path, body). The returned string is sent back
 * as the response body with ``Content-Type: application/json``.
 */
using http_route_handler = std::function<std::string (const std::string &method,
                                                      const std::string &path,
                                                      const std::string &body)>;

/**
 * Runs a blocking HTTP accept loop on ``0.0.0.0:port`` until
 * \p running becomes ``false``.
 *
 * One request per connection (``Connection: close``): the request
 * line, headers and the ``Content-Length``-delimited body are parsed
 * minimally, then handed to \p route. Uses plain POSIX sockets — no
 * libcurl, which has no server-side API.
 *
 * The loop wakes every 100 ms so ``stop()`` stays responsive, and
 * each client gets a 5 s read timeout so a stalled peer cannot hang
 * the listener.
 *
 * :param running: flipped to ``false`` by ``stop()`` to end the loop.
 * :param port: TCP port to bind (``SO_REUSEADDR`` is set).
 * :param log_name: prefix for log messages (e.g. ``"[a2a]"``).
 * :param route: request handler producing the response body.
 * :return: ``true`` once the loop ran and exited cleanly; ``false``
 *          if the socket could not be created, bound or listening.
 */
bool run_http_listener (std::atomic<bool> &running, uint16_t port,
                        const char *log_name, const http_route_handler &route);

} // namespace agentsdk::a2a::http
