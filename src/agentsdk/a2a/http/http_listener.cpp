#include <agentsdk/a2a/http/http_listener.hpp>

#include <spdlog/spdlog.h>

#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include <cctype>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <string>

// Linux provides MSG_NOSIGNAL; on other POSIX platforms sending to a
// closed peer may raise SIGPIPE instead.
#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif

namespace agentsdk::a2a::http
{

namespace
{

/** Upper bound on header bytes; guards against endless garbage. */
constexpr std::size_t k_max_header_bytes = 16 * 1024;

/** Upper bound on request body (A2A messages are small JSON docs). */
constexpr std::size_t k_max_body_bytes = 4 * 1024 * 1024;

/** Case-insensitive test: does \p line start with \p prefix? */
bool
starts_with_ci (const std::string &line, const char *prefix)
{
  std::size_t i = 0;
  for (; prefix[i] != '\0'; ++i) {
    if (i >= line.size ()
        || std::tolower (static_cast<unsigned char> (line[i]))
               != std::tolower (static_cast<unsigned char> (prefix[i]))) {
      return false;
    }
  }
  return true;
}

/** Send the whole buffer, tolerating partial writes. */
bool
send_all (int fd, const std::string &data)
{
  std::size_t sent = 0;
  while (sent < data.size ()) {
    const ssize_t n
        = send (fd, data.data () + sent, data.size () - sent, MSG_NOSIGNAL);
    if (n <= 0) {
      return false;
    }
    sent += static_cast<std::size_t> (n);
  }
  return true;
}

struct http_request
{
  std::string method;
  std::string path;
  std::string body;
  bool ok = false;
};

/** Read up to and including the blank line; empty on failure. */
std::string
read_headers (int fd)
{
  std::string header;
  char buf[2048];
  while (header.find ("\r\n\r\n") == std::string::npos) {
    if (header.size () > k_max_header_bytes) {
      return {};
    }
    const ssize_t n = recv (fd, buf, sizeof (buf), 0);
    if (n <= 0) {
      return {};
    }
    header.append (buf, static_cast<std::size_t> (n));
  }
  return header;
}

/** Parse the request line and the Content-Length-delimited body. */
http_request
read_request (int fd)
{
  http_request req;
  const std::string header = read_headers (fd);
  if (header.empty ()) {
    return req;
  }

  const std::size_t line_end = header.find ("\r\n");
  if (line_end == std::string::npos) {
    return req;
  }

  // Request line: METHOD SP PATH SP VERSION
  const std::string request_line = header.substr (0, line_end);
  const std::size_t sp1 = request_line.find (' ');
  const std::size_t sp2 = (sp1 == std::string::npos)
                              ? std::string::npos
                              : request_line.find (' ', sp1 + 1);
  if (sp1 == std::string::npos || sp2 == std::string::npos) {
    return req;
  }
  req.method = request_line.substr (0, sp1);
  req.path = request_line.substr (sp1 + 1, sp2 - sp1 - 1);

  // Content-Length (HTTP header names are case-insensitive).
  std::size_t content_length = 0;
  std::size_t pos = line_end + 2;
  while (pos + 1 < header.size ()) {
    const std::size_t eol = header.find ("\r\n", pos);
    if (eol == std::string::npos || eol == pos) {
      break; // blank line: end of headers
    }
    const std::string line = header.substr (pos, eol - pos);
    if (starts_with_ci (line, "Content-Length:")) {
      const std::size_t colon = line.find (':');
      content_length = std::strtoul (line.c_str () + colon + 1, nullptr, 10);
    }
    pos = eol + 2;
  }
  if (content_length > k_max_body_bytes) {
    return req;
  }

  // The first recv may have already read past the blank line; those
  // bytes are the beginning of the body.
  std::size_t const body_start = header.find ("\r\n\r\n") + 4;
  req.body = header.substr (body_start);
  while (req.body.size () < content_length) {
    char buf[4096];
    const std::size_t remaining = content_length - req.body.size ();
    const std::size_t chunk
        = remaining < sizeof (buf) ? remaining : sizeof (buf);
    const ssize_t n = recv (fd, buf, chunk, 0);
    if (n <= 0) {
      return req;
    }
    req.body.append (buf, static_cast<std::size_t> (n));
  }

  req.ok = true;
  return req;
}

} // namespace

bool
run_http_listener (std::atomic<bool> &running, uint16_t port,
                   const char *log_name, const http_route_handler &route)
{
  const int listen_fd = socket (AF_INET, SOCK_STREAM, 0);
  if (listen_fd < 0) {
    spdlog::error ("{} failed to create socket: {}", log_name,
                   std::strerror (errno));
    return false;
  }

  const int reuse = 1;
  setsockopt (listen_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof (reuse));

  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl (INADDR_ANY);
  addr.sin_port = htons (port);
  if (bind (listen_fd, reinterpret_cast<sockaddr *> (&addr), sizeof (addr))
      < 0) {
    spdlog::error ("{} failed to bind port {}: {}", log_name, port,
                   std::strerror (errno));
    close (listen_fd);
    return false;
  }

  if (listen (listen_fd, 8) < 0) {
    spdlog::error ("{} failed to listen: {}", log_name, std::strerror (errno));
    close (listen_fd);
    return false;
  }

  spdlog::info ("{} listening on 0.0.0.0:{}", log_name, port);

  while (running.load ()) {
    fd_set fds;
    FD_ZERO (&fds);
    FD_SET (listen_fd, &fds);
    timeval timeout{}; // wake at least every 100 ms to check `running`
    timeout.tv_usec = 100000;

    if (select (listen_fd + 1, &fds, nullptr, nullptr, &timeout) <= 0) {
      continue;
    }

    const int client_fd = accept (listen_fd, nullptr, nullptr);
    if (client_fd < 0) {
      continue;
    }

    // A stalled client must not hang the accept loop.
    timeval read_timeout{};
    read_timeout.tv_sec = 5;
    setsockopt (client_fd, SOL_SOCKET, SO_RCVTIMEO, &read_timeout,
                sizeof (read_timeout));

    const http_request req = read_request (client_fd);
    if (req.ok) {
      spdlog::debug ("{} {} {}", log_name, req.method, req.path);
      const std::string body = route (req.method, req.path, req.body);
      const std::string response = "HTTP/1.1 200 OK\r\n"
                                   "Content-Type: application/json\r\n"
                                   "Content-Length: "
                                   + std::to_string (body.size ())
                                   + "\r\n"
                                     "Connection: close\r\n"
                                     "\r\n"
                                   + body;
      send_all (client_fd, response);
    } else {
      static constexpr const char k_bad[]
          = "HTTP/1.1 400 Bad Request\r\n"
            "Content-Type: application/json\r\n"
            "Content-Length: 36\r\n"
            "Connection: close\r\n"
            "\r\n"
            "{\"error\":\"malformed HTTP request\"}";
      send_all (client_fd, std::string (k_bad));
    }
    close (client_fd);
  }

  close (listen_fd);
  return true;
}

} // namespace agentsdk::a2a::http
