#pragma once

#include <agentsdk/common/json_builder.hpp>
#include <agentsdk/common/result.hpp>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include <simdjson.h>

namespace agentsdk
{

/**
 * A JSON-RPC response awaiting completion.
 *
 * Shared by every protocol that speaks newline-delimited JSON-RPC over stdio
 * (ACP, MCP).  The read thread marks it completed and fills either `result`
 * or `error` with the raw JSON payload.
 */
struct json_rpc_response
{
  /** Raw JSON of the ``result`` member. */
  std::string result;
  /** Raw JSON of the ``error`` member. */
  std::string error;
  /** Set once a response (or a transport failure) has arrived. */
  bool completed = false;
};

/**
 * A failed JSON-RPC call: the raw error object.
 */
struct json_rpc_error
{
  /** Raw JSON of the error object (``{"code":...,"message":...}``). */
  std::string error_json;
};

/**
 * Base class for newline-delimited JSON-RPC 2.0 over stdio.
 *
 * Owns the child process, the pipe plumbing, the read loop and the
 * request/response correlation.  Subclasses implement :cpp:func:`on_line` to
 * route each complete message to protocol-specific handlers.
 *
 * Both the ACP client and the MCP client derive from this so that the
 * fork/pipe and correlation machinery is written once.
 */
class stdio_json_rpc
{
public:
  stdio_json_rpc ();
  virtual ~stdio_json_rpc ();

  stdio_json_rpc (const stdio_json_rpc &) = delete;
  stdio_json_rpc &operator= (const stdio_json_rpc &) = delete;

  /**
   * Launch a subprocess and connect to it over stdio.
   *
   * :param command: Path to the executable.
   * :param args: Command-line arguments.
   * :return: ``true`` on success.
   */
  bool launch (const std::string &command,
               const std::vector<std::string> &args);

  /**
   * Send a JSON-RPC request and block until the response arrives.
   *
   * Correlates the response by JSON-RPC id, never by FIFO position, so
   * responses may arrive in any order relative to other traffic.
   *
   * :param method: The RPC method name.
   * :param params_json: Serialized JSON params (or ``"{}"``).
   * :param timeout: How long to wait for the response.  A non-positive
   *   duration waits indefinitely.
   * :return: The raw result JSON, or an empty string on error or timeout.
   */
  std::string
  request (const std::string &method, const std::string &params_json,
           std::chrono::milliseconds timeout = std::chrono::seconds{ 30 });

  /**
   * Send a JSON-RPC request and preserve the error payload.
   *
   * Behaves like :cpp:func:`request`, but failures are returned as a
   * raw JSON-RPC error object instead of being collapsed to ``""``.
   * A timeout is reported as ``{"code":-32000,"message":"..."}`` and a
   * ``notifications/cancelled`` notification is emitted for the id, as
   * the MCP spec recommends.
   *
   * :return: The raw result JSON, or the raw error JSON on failure.
   */
  result<std::string, json_rpc_error>
  request_detailed (const std::string &method, const std::string &params_json,
                    std::chrono::milliseconds timeout
                    = std::chrono::seconds{ 30 });

  /**
   * Send a JSON-RPC notification (no response expected).
   *
   * :param method: The notification method name.
   * :param params_json: Serialized JSON params.
   */
  void notify (const std::string &method, const std::string &params_json);

  /**
   * Send a raw newline-delimited message.
   *
   * :param line: The message body; a newline is appended.
   */
  void write_line (const std::string &line);

  /** Check if the subprocess is alive. */
  bool is_running () const;

  /** Terminate the subprocess and close the pipes. */
  void terminate ();

protected:
  /**
   * Called on the read thread for each complete line.
   *
   * Implementations must not block for long: the read loop is stalled until
   * this returns.  Queue work for another thread if a handler may block.
   *
   * :param line: One newline-delimited message, without the newline.
   */
  virtual void on_line (const std::string &line) = 0;

  /** Whether the transport is currently running. */
  std::atomic<bool> m_running;

private:
  void read_loop ();
  void handle_line (const std::string &line);
  void complete_response (simdjson::dom::element id_el,
                          simdjson::dom::element doc);
  void complete_all_pending ();

  /**
   * Poll for a child exit until the timeout elapses.
   *
   * Reaps the child on success.  Returns ``true`` if the child exited.
   */
#ifndef _WIN32
  static bool wait_for_exit (pid_t pid, std::chrono::milliseconds timeout);
#else
  // Windows has no pid_t/signal/waitpid: the child is a HANDLE and waiting
  // is a single WaitForSingleObject.  The handle type stays void* so this
  // header remains windows.h-free.
  static bool wait_for_process (void *process,
                                std::chrono::milliseconds timeout);
#endif

#ifdef _WIN32
  // Child process and pipe HANDLEs, stored as void* (see above).
  void *m_hProcess = nullptr;
  void *m_hStdinWrite = nullptr;
  void *m_hStdoutRead = nullptr;
  std::uint32_t m_process_id = 0;
#else
  int m_stdin_fd = -1;
  int m_stdout_fd = -1;
  int m_agent_pid = -1;
#endif

  std::thread m_read_thread;

  std::mutex m_write_mutex;

  std::mutex m_pending_mutex;
  std::condition_variable m_pending_cv;
  std::unordered_map<int64_t, std::shared_ptr<json_rpc_response>> m_pending;

  std::atomic<int64_t> m_next_id{ 1 };
};

} // namespace agentsdk
