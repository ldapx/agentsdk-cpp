#pragma once

#include <agentsdk/common/stdio_json_rpc.hpp>
#include <agentsdk/mcp/mcp_json.hpp>
#include <agentsdk/mcp/mcp_types.hpp>

#include <chrono>
#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace agentsdk::mcp
{

/**
 * MCP client over stdio.
 *
 * Launches an MCP server as a subprocess and speaks newline-delimited
 * JSON-RPC 2.0 with it, following the 2025-06-18 lifecycle: initialize,
 * notifications/initialized, then operation.
 *
 * Subprocess plumbing and request/response correlation come from
 * :cpp:class:`agentsdk::stdio_json_rpc`; this class adds the MCP message
 * routing on top, including a dispatch thread for server-initiated requests
 * (sampling, roots, elicitation) so the read loop never stalls.
 */
class mcp_client : private stdio_json_rpc
{
public:
  /** Handler for sampling/createMessage requests from the server. */
  using sampling_handler = std::function<result<sampling_result, mcp_error> (
      const sampling_params &params)>;

  /** Handler for elicitation/create requests from the server. */
  using elicitation_handler
      = std::function<result<elicitation_result, mcp_error> (
          const elicitation_params &params)>;

  /** Handler for structured log messages from the server. */
  using log_handler = std::function<void (const log_message &msg)>;

  /** Handler for progress notifications. */
  using progress_handler = std::function<void (const progress_notification &n)>;

  /** Handler for cancellation notifications. */
  using cancelled_handler
      = std::function<void (const wire_id &id, const std::string &reason)>;

  mcp_client ();
  ~mcp_client () override;

  mcp_client (const mcp_client &) = delete;
  mcp_client &operator= (const mcp_client &) = delete;

  /**
   * Launch an MCP server subprocess and connect via stdio pipes.
   *
   * :param command: Path to the server executable.
   * :param args: Command-line arguments.
   * :return: true on success.
   */
  bool launch_server (const std::string &command,
                      const std::vector<std::string> &args);

  /**
   * Run the initialize handshake.
   *
   * Sends ``initialize`` and, on success, the ``notifications/initialized``
   * notification.  Must be called before any other operation.
   *
   * :param info: Client implementation information.
   * :param caps: Client capabilities.
   * :param timeout: How long to wait for the response.
   * :return: The server's initialize result, or an error.
   */
  result<initialize_result, mcp_error>
  initialize (const implementation_info &info, const client_capabilities &caps,
              std::chrono::milliseconds timeout = std::chrono::seconds{ 30 });

  /** Whether the initialize handshake completed. */
  bool is_initialized () const;

  /** Negotiated protocol version (empty before initialize). */
  std::string protocol_version () const;

  /** Server capabilities from the handshake. */
  server_capabilities server_caps () const;

  /** Server implementation info from the handshake. */
  implementation_info server_info () const;

  /** Optional instructions from the handshake. */
  std::optional<std::string> server_instructions () const;

  /** Check if the server process is alive. */
  bool is_running () const;

  /** Terminate the server process and close pipes. */
  void terminate ();

  // ── Standard operations ──────────────────────────────────────

  /**
   * Send a ping.
   */
  result<void, mcp_error> ping (std::chrono::milliseconds timeout
                                = std::chrono::seconds{ 30 });

  /**
   * List tools (one page).
   */
  result<tools_list_result, mcp_error>
  list_tools (const std::optional<std::string> &cursor = std::nullopt,
              std::chrono::milliseconds timeout = std::chrono::seconds{ 30 });

  /**
   * List all tools, following pagination cursors.
   */
  result<std::vector<tool>, mcp_error>
  list_all_tools (std::chrono::milliseconds timeout
                  = std::chrono::seconds{ 30 });

  /**
   * Call a tool.
   *
   * :param name: Tool name.
   * :param arguments_json: Arguments object as raw JSON (default ``"{}"``).
   */
  result<tool_result, mcp_error>
  call_tool (const std::string &name, const std::string &arguments_json = "{}",
             std::chrono::milliseconds timeout = std::chrono::seconds{ 30 });

  /**
   * List resources (one page).
   */
  result<resources_list_result, mcp_error>
  list_resources (const std::optional<std::string> &cursor = std::nullopt,
                  std::chrono::milliseconds timeout
                  = std::chrono::seconds{ 30 });

  /**
   * List all resources, following pagination cursors.
   */
  result<std::vector<resource>, mcp_error>
  list_all_resources (std::chrono::milliseconds timeout
                      = std::chrono::seconds{ 30 });

  /**
   * List resource templates (one page).
   */
  result<resource_templates_result, mcp_error> list_resource_templates (
      const std::optional<std::string> &cursor = std::nullopt,
      std::chrono::milliseconds timeout = std::chrono::seconds{ 30 });

  /**
   * Read a resource.
   */
  result<resource_read_result, mcp_error>
  read_resource (const std::string &uri, std::chrono::milliseconds timeout
                                         = std::chrono::seconds{ 30 });

  /**
   * Subscribe to resource updates.
   */
  result<void, mcp_error> subscribe_resource (const std::string &uri,
                                              std::chrono::milliseconds timeout
                                              = std::chrono::seconds{ 30 });

  /**
   * Unsubscribe from resource updates.
   */
  result<void, mcp_error>
  unsubscribe_resource (const std::string &uri,
                        std::chrono::milliseconds timeout
                        = std::chrono::seconds{ 30 });

  /**
   * List prompts (one page).
   */
  result<prompts_list_result, mcp_error>
  list_prompts (const std::optional<std::string> &cursor = std::nullopt,
                std::chrono::milliseconds timeout = std::chrono::seconds{ 30 });

  /**
   * List all prompts, following pagination cursors.
   */
  result<std::vector<prompt>, mcp_error>
  list_all_prompts (std::chrono::milliseconds timeout
                    = std::chrono::seconds{ 30 });

  /**
   * Get a prompt.
   *
   * :param name: Prompt name.
   * :param arguments_json: Arguments object as raw JSON, or nullopt.
   */
  result<prompt_result, mcp_error>
  get_prompt (const std::string &name,
              const std::optional<std::string> &arguments_json = std::nullopt,
              std::chrono::milliseconds timeout = std::chrono::seconds{ 30 });

  /**
   * Request argument completion.
   */
  result<completion_result, mcp_error>
  complete (const completion_params &params,
            std::chrono::milliseconds timeout = std::chrono::seconds{ 30 });

  /**
   * Set the server's minimum log level.
   */
  result<void, mcp_error> set_log_level (log_level level,
                                         std::chrono::milliseconds timeout
                                         = std::chrono::seconds{ 30 });

  // ── Client -> server notifications ───────────────────────────

  /**
   * Notify the server that the roots list changed.
   */
  void notify_roots_changed ();

  /**
   * Send a progress notification for a server-initiated request.
   */
  void notify_progress (const progress_notification &n);

  // ── Handlers ─────────────────────────────────────────────────

  /**
   * Set the handler for sampling/createMessage requests.
   *
   * Invoked on the dispatch thread; may block briefly.
   */
  void set_sampling_handler (sampling_handler handler);

  /**
   * Set the handler for elicitation/create requests.
   *
   * Invoked on the dispatch thread; may block briefly.
   */
  void set_elicitation_handler (elicitation_handler handler);

  /**
   * Set the static roots list answered to roots/list requests.
   */
  void set_roots (const std::vector<root> &roots);

  /**
   * Set the handler for notifications/message (log messages).
   */
  void set_log_handler (log_handler handler);

  /**
   * Set the handler for notifications/tools/list_changed.
   */
  void set_tools_changed_handler (std::function<void ()> handler);

  /**
   * Set the handler for notifications/resources/list_changed.
   */
  void set_resources_changed_handler (std::function<void ()> handler);

  /**
   * Set the handler for notifications/resources/updated.
   */
  void set_resource_updated_handler (
      std::function<void (const std::string &)> handler);

  /**
   * Set the handler for notifications/prompts/list_changed.
   */
  void set_prompts_changed_handler (std::function<void ()> handler);

  /**
   * Set the handler for notifications/progress.
   */
  void set_progress_handler (progress_handler handler);

  /**
   * Set the handler for notifications/cancelled.
   */
  void set_cancelled_handler (cancelled_handler handler);

private:
  struct incoming_request
  {
    std::string id_json;
    std::string method;
    std::string params;
  };

  void on_line (const std::string &line) override;

  void dispatch_message (const std::string &line);
  void handle_notification (const std::string &method,
                            const std::string &params);
  void start_dispatch_thread ();
  void stop_dispatch_thread ();
  void process_dispatch_queue ();
  void handle_incoming_request (const incoming_request &request);

  /** Call a method and parse the result; maps transport failure to error. */
  result<std::string, mcp_error> call (const std::string &method,
                                       const std::string &params_json,
                                       std::chrono::milliseconds timeout);

  result<std::string, mcp_error> require_initialized () const;

  mutable std::mutex m_state_mutex;
  bool m_initialized = false;
  std::string m_protocol_version;
  server_capabilities m_server_caps;
  implementation_info m_server_info;
  std::optional<std::string> m_instructions;

  std::mutex m_handler_mutex;
  sampling_handler m_on_sampling;
  elicitation_handler m_on_elicitation;
  std::vector<root> m_roots;
  log_handler m_on_log;
  std::function<void ()> m_on_tools_changed;
  std::function<void ()> m_on_resources_changed;
  std::function<void (const std::string &)> m_on_resource_updated;
  std::function<void ()> m_on_prompts_changed;
  progress_handler m_on_progress;
  cancelled_handler m_on_cancelled;

  std::thread m_dispatch_thread;
  std::mutex m_dispatch_mutex;
  std::condition_variable m_dispatch_cv;
  std::deque<incoming_request> m_dispatch_queue;
};

} // namespace agentsdk::mcp
