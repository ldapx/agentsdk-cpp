#include <agentsdk/common/stdio_json_rpc.hpp>

#include <spdlog/spdlog.h>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <fcntl.h>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>
#endif

#include <cctype>
#include <cstring>

namespace agentsdk
{

#ifdef _WIN32
namespace
{

/**
 * Quote one argv element for CreateProcess command lines.
 *
 * :param arg: Raw argument value.
 * :return: ``arg`` quoted when it contains whitespace or quotes.
 */
std::string
quote_arg (const std::string &arg)
{
  if (arg.empty ()) {
    return "\"\"";
  }
  bool quote = false;
  for (char c : arg) {
    if (std::isspace (static_cast<unsigned char> (c)) || c == '"') {
      quote = true;
      break;
    }
  }
  if (!quote) {
    return arg;
  }
  std::string out = "\"";
  for (char c : arg) {
    if (c == '"') {
      out += '\\';
    }
    out += c;
  }
  out += '"';
  return out;
}

} // namespace
#endif

stdio_json_rpc::stdio_json_rpc () = default;

stdio_json_rpc::~stdio_json_rpc () { terminate (); }

bool
stdio_json_rpc::launch (const std::string &command,
                        const std::vector<std::string> &args)
{
  if (m_running.load ()) {
    terminate ();
  }

#ifdef _WIN32
  // Windows: CreateProcess with redirected stdio.  Mirrors the POSIX
  // fork/exec/pipe setup below (stdin/stdout pipes, stderr to NUL).
  SECURITY_ATTRIBUTES sa{};
  sa.nLength = sizeof (sa);
  sa.bInheritHandle = TRUE;

  HANDLE hStdinRead = nullptr, hStdinWrite = nullptr;
  HANDLE hStdoutRead = nullptr, hStdoutWrite = nullptr;
  if (!CreatePipe (&hStdinRead, &hStdinWrite, &sa, 0)
      || !CreatePipe (&hStdoutRead, &hStdoutWrite, &sa, 0)) {
    spdlog::error ("[stdio-json-rpc] Failed to create pipes");
    return false;
  }
  // Only the child's ends are inherited.
  SetHandleInformation (hStdinWrite, HANDLE_FLAG_INHERIT, 0);
  SetHandleInformation (hStdoutRead, HANDLE_FLAG_INHERIT, 0);

  HANDLE hNull
      = CreateFileA ("NUL", GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                     &sa, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);

  std::string cmdline = command;
  for (auto &a : args) {
    cmdline += ' ';
    cmdline += quote_arg (a);
  }

  STARTUPINFOA si{};
  si.cb = sizeof (si);
  si.dwFlags = STARTF_USESTDHANDLES;
  si.hStdInput = hStdinRead;
  si.hStdOutput = hStdoutWrite;
  si.hStdError = hNull ? hNull : GetStdHandle (STD_ERROR_HANDLE);
  PROCESS_INFORMATION pi{};
  const BOOL started
      = CreateProcessA (nullptr, cmdline.data (), nullptr, nullptr, TRUE,
                        CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi);
  CloseHandle (hStdinRead);
  CloseHandle (hStdoutWrite);
  if (hNull) {
    CloseHandle (hNull);
  }
  if (!started) {
    spdlog::error ("[stdio-json-rpc] Failed to exec {}: {}", command,
                   static_cast<unsigned long> (GetLastError ()));
    CloseHandle (hStdinWrite);
    CloseHandle (hStdoutRead);
    return false;
  }

  m_hStdinWrite = hStdinWrite;
  m_hStdoutRead = hStdoutRead;
  m_hProcess = pi.hProcess;
  m_process_id = pi.dwProcessId;
  CloseHandle (pi.hThread);
  m_running.store (true);

  m_read_thread = std::thread ([this] { read_loop (); });

  spdlog::info ("[stdio-json-rpc] Process launched: {} (pid {})", command,
                m_process_id);
  return true;
#else
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
#endif
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

#ifdef _WIN32
  // Same escalation as POSIX below: EOF first, then kill.  Windows has no
  // SIGTERM: TerminateProcess is the only step, and closing the process
  // handle reaps it (no zombies on Windows).
  if (m_hProcess) {
    HANDLE proc = static_cast<HANDLE> (m_hProcess);

    if (m_hStdinWrite) {
      CloseHandle (static_cast<HANDLE> (m_hStdinWrite));
      m_hStdinWrite = nullptr;
    }

    if (!wait_for_process (proc, std::chrono::milliseconds{ 200 })) {
      TerminateProcess (proc, 1);
      WaitForSingleObject (proc, INFINITE);
    }
    CloseHandle (proc);
    m_hProcess = nullptr;
  } else if (m_hStdinWrite) {
    CloseHandle (static_cast<HANDLE> (m_hStdinWrite));
    m_hStdinWrite = nullptr;
  }

  // Join before closing the stdout pipe, mirroring the POSIX EBADF note.
  if (m_read_thread.joinable ()) {
    m_read_thread.join ();
  }

  if (m_hStdoutRead) {
    CloseHandle (static_cast<HANDLE> (m_hStdoutRead));
    m_hStdoutRead = nullptr;
  }

  complete_all_pending ();
#else
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
#endif
}

#ifndef _WIN32
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
#else
bool
stdio_json_rpc::wait_for_process (void *process,
                                  std::chrono::milliseconds timeout)
{
  const auto ms = timeout.count ();
  const DWORD wait_ms = ms < 0 ? 0 : static_cast<DWORD> (ms);
  return WaitForSingleObject (static_cast<HANDLE> (process), wait_ms)
         == WAIT_OBJECT_0;
}
#endif

void
stdio_json_rpc::read_loop ()
{
  std::string buffer;
  char chunk[4096];

  while (m_running.load ()) {
#ifdef _WIN32
    DWORD n = 0;
    if (!ReadFile (static_cast<HANDLE> (m_hStdoutRead), chunk,
                   sizeof (chunk) - 1, &n, nullptr)
        || n == 0) {
      break;
    }
#else
    ssize_t n = read (m_stdout_fd, chunk, sizeof (chunk) - 1);
    if (n <= 0) {
      if (n < 0 && errno == EINTR)
        continue;
      break;
    }
#endif

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
#ifdef _WIN32
  HANDLE out = static_cast<HANDLE> (m_hStdinWrite);
  if (!out) {
    return;
  }

  std::string msg = line + "\n";
  std::size_t written = 0;
  while (written < msg.size ()) {
    DWORD n = 0;
    if (!WriteFile (out, msg.c_str () + written,
                    static_cast<DWORD> (msg.size () - written), &n, nullptr)
        || n == 0) {
      spdlog::error ("[stdio-json-rpc] Write to stdin failed: {}",
                     static_cast<unsigned long> (GetLastError ()));
      break;
    }
    written += n;
  }
#else
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
#endif
}

} // namespace agentsdk
