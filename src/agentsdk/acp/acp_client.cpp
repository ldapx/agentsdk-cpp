#include <agentsdk/a2a/json_util.hpp>

#include <agentsdk/acp/acp_client.hpp>

#include <spdlog/spdlog.h>

#include <fcntl.h>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>

#include <cstring>

namespace agentsdk::acp
{

acp_client::acp_client () = default;

acp_client::~acp_client () { terminate (); }

bool
acp_client::launch_agent (const std::string &command,
                          const std::vector<std::string> &args)
{
  if (m_running.load ()) {
    terminate ();
  }

  int stdin_pipe[2];
  int stdout_pipe[2];

  if (pipe (stdin_pipe) != 0 || pipe (stdout_pipe) != 0) {
    spdlog::error ("[acp] Failed to create pipes");
    return false;
  }

  int stderr_fd = open ("/dev/null", O_WRONLY);
  if (stderr_fd < 0) {
    stderr_fd = dup (STDOUT_FILENO);
  }

  pid_t pid = fork ();
  if (pid < 0) {
    spdlog::error ("[acp] Failed to fork: {}", strerror (errno));
    close (stdin_pipe[0]);
    close (stdin_pipe[1]);
    close (stdout_pipe[0]);
    close (stdout_pipe[1]);
    return false;
  }

  if (pid == 0) {
    // Child process: agent
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
    fprintf (stderr, "[acp] Failed to exec %s: %s\n", command.c_str (),
             strerror (errno));
    _exit (1);
  }

  // Parent process: editor
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
  start_dispatch_thread ();

  spdlog::info ("[acp] Agent launched: {} (pid {})", command, pid);
  return true;
}

std::string
acp_client::send_request (const std::string &method,
                          const std::string &params_json,
                          std::chrono::milliseconds timeout)
{
  int64_t id = m_next_id.fetch_add (1);

  a2a::json_builder jb;
  jb.begin_object ();
  jb.add_string ("jsonrpc", "2.0");
  jb.add_int ("id", id);
  jb.add_string ("method", method);
  jb.add_raw_json ("params", params_json);
  jb.end_object ();

  auto pending = std::make_shared<pending_request> ();
  {
    std::lock_guard<std::mutex> lock (m_pending_mutex);
    m_pending[id] = pending;
  }

  write_line (jb.str ());

  // Wait for response
  std::unique_lock<std::mutex> lock (m_pending_mutex);
  if (timeout > std::chrono::milliseconds::zero ()) {
    m_pending_cv.wait_for (lock, timeout, [&] { return pending->completed; });
  } else {
    m_pending_cv.wait (lock, [&] { return pending->completed; });
  }

  std::string result;
  if (pending->completed) {
    result = pending->result;
    if (!pending->error.empty ()) {
      spdlog::warn ("[acp] Request {} ({}) failed: {}", id, method,
                    pending->error);
      result.clear ();
    }
  } else {
    spdlog::warn ("[acp] Request {} ({}) timed out", id, method);
  }

  m_pending.erase (id);

  return result;
}

void
acp_client::send_notification (const std::string &method,
                               const std::string &params_json)
{
  a2a::json_builder jb;
  jb.begin_object ();
  jb.add_string ("jsonrpc", "2.0");
  jb.add_string ("method", method);
  jb.add_raw_json ("params", params_json);
  jb.end_object ();

  write_line (jb.str ());
}

void
acp_client::set_notification_handler (notification_handler handler)
{
  m_on_notification = std::move (handler);
}

void
acp_client::set_agent_request_handler (
    std::function<agent_response (const std::string &method,
                                  const std::string &params)>
        handler)
{
  m_on_agent_request = std::move (handler);
}

bool
acp_client::is_running () const
{
  return m_running.load ();
}

void
acp_client::terminate ()
{
  m_running.store (false);

  if (m_agent_pid > 0) {
    kill (m_agent_pid, SIGTERM);
    waitpid (m_agent_pid, nullptr, WNOHANG);
    m_agent_pid = -1;
  }

  stop_dispatch_thread ();

  if (m_stdin_fd >= 0) {
    close (m_stdin_fd);
    m_stdin_fd = -1;
  }

  if (m_stdout_fd >= 0) {
    close (m_stdout_fd);
    m_stdout_fd = -1;
  }

  if (m_read_thread.joinable ()) {
    m_read_thread.join ();
  }

  // Wake up any pending requests
  complete_all_pending ();
}

void
acp_client::read_loop ()
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

    // Split on newlines (ACP stdio delimiter)
    while (true) {
      auto pos = buffer.find ('\n');
      if (pos == std::string::npos)
        break;

      std::string line = buffer.substr (0, pos);
      buffer.erase (0, pos + 1);

      if (!line.empty ()) {
        dispatch_message (line);
      }
    }
  }

  spdlog::info ("[acp] Agent stdout closed");
  m_running.store (false);

  // Unblock callers waiting for responses that will never arrive
  complete_all_pending ();
}

void
acp_client::dispatch_message (const std::string &line)
{
  thread_local simdjson::dom::parser parser;
  auto doc = parser.parse (line);

  // Extract jsonrpc version
  auto jsonrpc = doc["jsonrpc"];
  std::string_view jsonrpc_val;
  if (jsonrpc.error () || jsonrpc.get_string ().get (jsonrpc_val) != 0
      || jsonrpc_val != "2.0") {
    spdlog::warn ("[acp] Invalid JSON-RPC message");
    return;
  }

  // Determine if this is a response or notification
  auto id_el = doc["id"];
  auto method_el = doc["method"];

  if (!id_el.error () && !method_el.error ()) {
    // Request from the agent (permission prompts, fs access, ...).
    // Queue it for the dispatch thread so the read loop keeps
    // processing messages while the handler runs.
    incoming_request request;
    request.id_json = simdjson::to_string (id_el.value ());
    request.method = std::string (method_el.get_string ().value ());

    auto params_el = doc["params"];
    if (!params_el.error ()) {
      request.params = simdjson::to_string (params_el.value ());
    }

    spdlog::info ("[acp] Agent request: {}", request.method);

    {
      std::lock_guard<std::mutex> lock (m_dispatch_mutex);
      m_dispatch_queue.push_back (std::move (request));
    }
    m_dispatch_cv.notify_one ();
    return;
  }

  if (!id_el.error ()) {
    // Response to our request
    int64_t id = 0;
    if (id_el.get_int64 ().get (id) != 0) {
      // Try string id
      auto id_str = id_el.get_string ();
      if (!id_str.error ()) {
        try {
          id = std::stoll (std::string (id_str.value ()));
        } catch (...) {
          spdlog::warn ("[acp] Cannot parse response id");
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
      spdlog::warn ("[acp] No pending request for id={}", id);
    }
    return;
  }

  if (!method_el.error ()) {
    // Notification from agent
    std::string method (method_el.get_string ().value ());
    std::string params;
    auto params_el = doc["params"];
    if (!params_el.error ()) {
      params = simdjson::to_string (params_el.value ());
    }

    if (m_on_notification) {
      m_on_notification (method, params);
    }
    return;
  }

  spdlog::warn ("[acp] Malformed JSON-RPC message");
}

void
acp_client::start_dispatch_thread ()
{
  if (!m_dispatch_thread.joinable ()) {
    m_dispatch_thread = std::thread ([this] { process_dispatch_queue (); });
  }
}

void
acp_client::stop_dispatch_thread ()
{
  // m_running is false by now; wake the loop so it can exit.
  m_dispatch_cv.notify_all ();

  if (m_dispatch_thread.joinable ()) {
    m_dispatch_thread.join ();
  }
}

void
acp_client::process_dispatch_queue ()
{
  while (true) {
    incoming_request request;

    {
      std::unique_lock<std::mutex> lock (m_dispatch_mutex);
      m_dispatch_cv.wait (lock, [this] {
        return !m_running.load () || !m_dispatch_queue.empty ();
      });

      if (!m_running.load () && m_dispatch_queue.empty ()) {
        break;
      }

      request = std::move (m_dispatch_queue.front ());
      m_dispatch_queue.pop_front ();
    }

    handle_incoming_request (request);
  }
}

void
acp_client::handle_incoming_request (const incoming_request &request)
{
  agent_response response;
  if (m_on_agent_request) {
    response = m_on_agent_request (request.method, request.params);
  }

  if (!response.handled) {
    spdlog::warn ("[acp] Unhandled agent request: {}", request.method);
  }

  a2a::json_builder jb;
  jb.begin_object ();
  jb.add_string ("jsonrpc", "2.0");
  jb.add_raw_json ("id", request.id_json);

  if (response.handled && response.ok) {
    jb.add_raw_json ("result", response.result_json);
  } else {
    jb.begin_object ("error");
    jb.add_int ("code", response.handled ? response.error_code : -32601);
    jb.add_string ("message",
                   response.handled
                       ? response.error_message
                       : "Method not handled by client: " + request.method);
    jb.end_object ();
  }

  jb.end_object ();
  write_line (jb.str ());
}

void
acp_client::complete_all_pending ()
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
acp_client::write_line (const std::string &line)
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
      spdlog::error ("[acp] Write to stdin failed: {}", strerror (errno));
      break;
    }
    written += n;
  }
}

} // namespace agentsdk::acp
