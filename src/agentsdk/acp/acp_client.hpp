#pragma once

#include <agentsdk/acp/acp_types.hpp>
#include <agentsdk/common/stdio_json_rpc.hpp>

#include <chrono>
#include <condition_variable>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace agentsdk::acp
{

/**
 * JSON-RPC 2.0 client over stdio (stdin/stdout).
 *
 * Manages an agent subprocess and provides synchronous request/response
 * plus asynchronous notification handling.
 *
 * Messages are newline-delimited JSON-RPC as required by ACP v1.
 *
 * The subprocess plumbing and request/response correlation come from
 * :cpp:class:`agentsdk::stdio_json_rpc`; this class adds the ACP message
 * routing on top.
 */
class acp_client : private stdio_json_rpc
{
public:
  using notification_handler = std::function<void (const std::string &method,
                                                   const std::string &params)>;

  acp_client ();
  ~acp_client () override;

  acp_client (const acp_client &) = delete;
  acp_client &operator= (const acp_client &) = delete;

  /**
   * Launch an agent subprocess and connect via stdio pipes.
   *
   * :param command: Path to the agent executable.
   * :param args: Command-line arguments.
   * :return: true on success.
   */
  bool launch_agent (const std::string &command,
                     const std::vector<std::string> &args);

  /**
   * Send a JSON-RPC request and block until the response arrives.
   *
   * :param method: The RPC method name.
   * :param params_json: Serialized JSON params (or "{}").
   * :param timeout: How long to wait for the response.  A non-positive
   *   duration waits indefinitely (the wait is still woken by a
   *   response, cancellation via terminate(), or agent death).
   * :return: The result JSON string, or empty on error/timeout.
   */
  std::string
  send_request (const std::string &method, const std::string &params_json,
                std::chrono::milliseconds timeout = std::chrono::seconds{ 30 });

  /**
   * Send a JSON-RPC notification (no response expected).
   *
   * :param method: The notification method name.
   * :param params_json: Serialized JSON params.
   */
  void send_notification (const std::string &method,
                          const std::string &params_json);

  /**
   * Set the handler for incoming notifications from the agent.
   */
  void set_notification_handler (notification_handler handler);

  /**
   * Set the handler for requests sent by the agent (permission prompts,
   * filesystem access, terminal, ...).
   *
   * Invoked on a background dispatch thread, serialized in arrival
   * order.  Handlers may block briefly (e.g. waiting for a user
   * decision) but must not call send_request() from within the handler.
   */
  void set_agent_request_handler (
      std::function<agent_response (const std::string &method,
                                    const std::string &params)>
          handler);

  /**
   * Check if the agent process is alive.
   */
  bool is_running () const;

  /**
   * Terminate the agent process and close pipes.
   */
  void terminate ();

private:
  /**
   * A request received from the agent, awaiting a reply.
   */
  struct incoming_request
  {
    std::string id_json;
    std::string method;
    std::string params;
  };

  void on_line (const std::string &line) override;

  void dispatch_message (const std::string &line);
  void start_dispatch_thread ();
  void stop_dispatch_thread ();
  void process_dispatch_queue ();
  void handle_incoming_request (const incoming_request &request);

  // Notification handler
  notification_handler m_on_notification;

  // Agent-to-client request handler and its serialized dispatch loop
  std::function<agent_response (const std::string &method,
                                const std::string &params)>
      m_on_agent_request;
  std::thread m_dispatch_thread;
  std::mutex m_dispatch_mutex;
  std::condition_variable m_dispatch_cv;
  std::deque<incoming_request> m_dispatch_queue;
};

} // namespace agentsdk::acp
