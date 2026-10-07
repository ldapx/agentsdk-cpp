#include <agentsdk/common/stdio_json_rpc.hpp>

#include <spdlog/spdlog.h>

#include <fcntl.h>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>

#include <cstring>

namespace agentsdk
{

stdio_json_rpc::stdio_json_rpc () = default;

stdio_json_rpc::~stdio_json_rpc () { terminate (); }

bool
stdio_json_rpc::launch (const std::string &command,
                        const std::vector<std::string> &args)
{
  if (m_running.load ()) {
    terminate ();
  }

  int stdin_pipe[2];
  int stdout_pipe[2];

  if (pipe (stdin_pipe) != 0 || pipe (stdout_pipe) != 0) {
    spdlog::error ("[stdio-json-rpc] Failed to create pipes");
    return false;
  }

  int stderr_fd = open ("/dev/null", O_WRONLY);
  if (stderr_fd < 0) {
    stderr_fd = dup (STDOUT_FILENO);
  }

  pid_t pid = fork ();
  if (pid < 0) {
    spdlog::error ("[stdio-json-rpc] Failed to fork: {}", strerror (errno));
    close (stdin_pipe[0]);
    close (stdin_pipe[1]);
    close (stdout_pipe[0]);
    close (stdout_pipe[1]);
    return false;
  }

  if (pid == 0) {
    // Child process: the server.
    close (stdin_pipe[1]);
    close (stdout_pipe[0]);
    close (stderr_fd);

    dup2 (stdin_pipe[0], STDIN_FILENO);
    dup2 (stdout_pipe[1], STDOUT_FILENO);

    close (stdin_pipe[0]);
    close (stdout_pipe[1]);

    std::vector<char *> argv;
    argv.push_back (const_cast<char *> (command.c_str ()));
    for (auto &a : args) {
      argv.push_back (const_cast<char *> (a.c_str ()));
    }
    argv.push_back (nullptr);

    execvp (command.c_str (), argv.data ());
    fprintf (stderr, "[stdio-json-rpc] Failed to exec %s: %s\n",
             command.c_str (), strerror (errno));
    _exit (1);
  }

  // Parent process.
  close (stdin_pipe[0]);
  close (stdout_pipe[1]);
  if (stderr_fd >= 0) {
    close (stderr_fd);
  }

  m_stdin_fd = stdin_pipe[1];
  m_stdout_fd = stdout_pipe[0];
  m_agent_pid = pid;
  m_running.store (true);

  m_read_thread = std::thread ([this] { read_loop (); });

  spdlog::info ("[stdio-json-rpc] Process launched: {} (pid {})", command, pid);
  return true;
}

std::string
stdio_json_rpc::request (const std::string &method,
                         const std::string &params_json,
                         std::chrono::milliseconds timeout)
{
  auto r = request_detailed (method, params_json, timeout);
  if (!r) {
    return "";
  }
  return r.value ();
}

result<std::string, json_rpc_error>
stdio_json_rpc::request_detailed (const std::string &method,
                                  const std::string &params_json,
                                  std::chrono::milliseconds timeout)
{
  int64_t id = m_next_id.fetch_add (1);

  json_builder jb;
  jb.begin_object ();
  jb.add_string ("jsonrpc", "2.0");
  jb.add_int ("id", id);
  jb.add_string ("method", method);
  jb.add_raw_json ("params", params_json);
  jb.end_object ();

  auto pending = std::make_shared<json_rpc_response> ();
  {
    std::lock_guard<std::mutex> lock (m_pending_mutex);
    m_pending[id] = pending;
  }

  write_line (jb.str ());

  // Wait for the response.
  std::unique_lock<std::mutex> lock (m_pending_mutex);
  if (timeout > std::chrono::milliseconds::zero ()) {
    m_pending_cv.wait_for (lock, timeout, [&] { return pending->completed; });
  } else {
    m_pending_cv.wait (lock, [&] { return pending->completed; });
  }

  std::string result_json;
  std::string error_json;
  if (pending->completed) {
    result_json = pending->result;
    error_json = pending->error;
  }

  m_pending.erase (id);
  lock.unlock ();

  if (!pending->completed) {
    spdlog::warn ("[stdio-json-rpc] Request {} ({}) timed out", id, method);
    // Tell the peer to stop work, per the MCP cancellation guidance.
    json_builder cb;
    cb.begin_object ();
    cb.add_string ("jsonrpc", "2.0");
    cb.add_string ("method", "notifications/cancelled");
    cb.begin_object ("params");
    cb.add_int ("requestId", id);
    cb.add_string ("reason", "request timed out");
    cb.end_object ();
    cb.end_object ();
    write_line (cb.str ());
    return json_rpc_error{
      "{\"code\":-32000,\"message\":\"request timed out\"}"
    };
  }

  if (!error_json.empty ()) {
    spdlog::warn ("[stdio-json-rpc] Request {} ({}) failed: {}", id, method,
                  error_json);
    return json_rpc_error{ error_json };
  }

  return result<std::string, json_rpc_error>{ result_json };
}

void
stdio_json_rpc::notify (const std::string &method,
                        const std::string &params_json)
{
  json_builder jb;
  jb.begin_object ();
  jb.add_string ("jsonrpc", "2.0");
  jb.add_string ("method", method);
  jb.add_raw_json ("params", params_json);
  jb.end_object ();

  write_line (jb.str ());
}

bool
stdio_json_rpc::is_running () const
{
  return m_running.load ();
}

void
stdio_json_rpc::terminate ()
{
  m_running.store (false);

  // Graceful shutdown, per the MCP stdio guidance: close stdin so a
  // well-behaved server sees EOF and exits, escalate to SIGTERM and then
  // SIGKILL only if it lingers.  The child is always reaped so no zombie
  // is left behind, and its exit is what unblocks the read thread.
  if (m_agent_pid > 0) {
    const pid_t pid = m_agent_pid;

    if (m_stdin_fd >= 0) {
      close (m_stdin_fd);
      m_stdin_fd = -1;
    }

    if (!wait_for_exit (pid, std::chrono::milliseconds{ 200 })) {
      kill (pid, SIGTERM);
      if (!wait_for_exit (pid, std::chrono::milliseconds{ 1000 })) {
        kill (pid, SIGKILL);
        waitpid (pid, nullptr, 0);
      }
    }
    m_agent_pid = -1;
  } else if (m_stdin_fd >= 0) {
    close (m_stdin_fd);
    m_stdin_fd = -1;
  }

  // Join before closing the stdout pipe: closing it first would race the
  // blocked read() and surface as EBADF instead of a clean EOF.
  if (m_read_thread.joinable ()) {
    m_read_thread.join ();
  }

  if (m_stdout_fd >= 0) {
    close (m_stdout_fd);
    m_stdout_fd = -1;
  }

  // Unblock callers waiting for responses that will never arrive.
  complete_all_pending ();
}

bool
stdio_json_rpc::wait_for_exit (pid_t pid, std::chrono::milliseconds timeout)
{
  const auto deadline = std::chrono::steady_clock::now () + timeout;
  while (std::chrono::steady_clock::now () < deadline) {
    int status = 0;
    if (waitpid (pid, &status, WNOHANG) == pid) {
      return true;
    }
    std::this_thread::sleep_for (std::chrono::milliseconds{ 5 });
  }
  int status = 0;
  return waitpid (pid, &status, WNOHANG) == pid;
}

void
stdio_json_rpc::read_loop ()
{
  std::string buffer;
  char chunk[4096];

  while (m_running.load ()) {
    ssize_t n = read (m_stdout_fd, chunk, sizeof (chunk) - 1);
    if (n <= 0) {
      if (n < 0 && errno == EINTR)
        continue;
      break;
    }

    chunk[n] = '\0';
    buffer.append (chunk, static_cast<size_t> (n));

    // Split on newlines (the stdio framing delimiter).
    while (true) {
      auto pos = buffer.find ('\n');
      if (pos == std::string::npos)
        break;

      std::string line = buffer.substr (0, pos);
      buffer.erase (0, pos + 1);

      if (!line.empty ()) {
        handle_line (line);
      }
    }
  }

  spdlog::info ("[stdio-json-rpc] stdout closed");
  m_running.store (false);

  complete_all_pending ();
}

void
stdio_json_rpc::handle_line (const std::string &line)
{
  thread_local simdjson::dom::parser parser;
  auto parsed = parser.parse (line);
  if (parsed.error ()) {
    spdlog::warn ("[stdio-json-rpc] Invalid JSON: {}",
                  simdjson::error_message (parsed.error ()));
    return;
  }
  auto doc = parsed.value ();

  auto id_el = doc["id"];
  auto method_el = doc["method"];

  // A response to one of our requests carries an id and no method.  It is
  // correlated here so that subclasses only ever see notifications and
  // peer-initiated requests.
  if (!id_el.error () && method_el.error ()) {
    complete_response (id_el.value (), doc);
    return;
  }

  on_line (line);
}

void
stdio_json_rpc::complete_response (simdjson::dom::element id_el,
                                   simdjson::dom::element doc)
{
  int64_t id = 0;
  if (id_el.get_int64 ().get (id) != 0) {
    // Try a string id.
    auto id_str = id_el.get_string ();
    if (!id_str.error ()) {
      try {
        id = std::stoll (std::string (id_str.value ()));
      } catch (...) {
        spdlog::warn ("[stdio-json-rpc] Cannot parse response id");
        return;
      }
    }
  }

  std::string result_str;
  std::string error_str;

  auto result_el = doc["result"];
  auto error_el = doc["error"];

  if (!result_el.error ()) {
    result_str = simdjson::to_string (result_el.value ());
  }
  if (!error_el.error ()) {
    error_str = simdjson::to_string (error_el.value ());
  }

  std::lock_guard<std::mutex> lock (m_pending_mutex);
  auto it = m_pending.find (id);
  if (it != m_pending.end ()) {
    it->second->result = std::move (result_str);
    it->second->error = std::move (error_str);
    it->second->completed = true;
    m_pending_cv.notify_all ();
  } else {
    spdlog::warn ("[stdio-json-rpc] No pending request for id={}", id);
  }
}

void
stdio_json_rpc::complete_all_pending ()
{
  {
    std::lock_guard<std::mutex> lock (m_pending_mutex);
    for (auto &[id, req] : m_pending) {
      req->completed = true;
    }
  }
  m_pending_cv.notify_all ();
}

void
stdio_json_rpc::write_line (const std::string &line)
{
  std::lock_guard<std::mutex> lock (m_write_mutex);
  if (m_stdin_fd < 0)
    return;

  std::string msg = line + "\n";
  ssize_t written = 0;
  while (written < static_cast<ssize_t> (msg.size ())) {
    ssize_t n = ::write (m_stdin_fd, msg.c_str () + written,
                         msg.size () - static_cast<size_t> (written));
    if (n < 0) {
      if (errno == EINTR)
        continue;
      spdlog::error ("[stdio-json-rpc] Write to stdin failed: {}",
                     strerror (errno));
      break;
    }
    written += n;
  }
}

} // namespace agentsdk
