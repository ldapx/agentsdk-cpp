#pragma once

#include <agentsdk/mcp/mcp_json.hpp>
#include <agentsdk/mcp/mcp_types.hpp>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace agentsdk::mcp
{

/**
 * MCP server over stdio.
 *
 * Reads newline-delimited JSON-RPC 2.0 from stdin and writes responses to
 * stdout, following the 2025-06-18 lifecycle: the client sends
 * ``initialize``, the server answers with version negotiation, then the
 * client sends ``notifications/initialized`` and operation begins.
 *
 * User code supplies handlers for tools, resources, prompts and completion;
 * the server adds sampling, roots and elicitation requests toward the
 * client, plus log and change notifications.
 *
 * The I/O loop (:cpp:func:`run`) blocks on stdin until EOF.  All message
 * routing lives in :cpp:func:`handle_line`, which is public so it can be
 * unit-tested without a subprocess.
 */
class mcp_server
{
public:
  /** List handler: given an optional cursor, return one page. */
  using list_tools_handler
      = std::function<result<tools_list_result, mcp_error> (
          const std::optional<std::string> &cursor)>;

  /** Tool call handler: raw arguments object in, tool result out. */
  using call_tool_handler = std::function<result<tool_result, mcp_error> (
      const std::string &name, const std::string &arguments_json)>;

  using list_resources_handler
      = std::function<result<resources_list_result, mcp_error> (
          const std::optional<std::string> &cursor)>;

  using list_templates_handler
      = std::function<result<resource_templates_result, mcp_error> (
          const std::optional<std::string> &cursor)>;

  using read_resource_handler
      = std::function<result<resource_read_result, mcp_error> (
          const std::string &uri)>;

  using subscribe_handler
      = std::function<result<void, mcp_error> (const std::string &uri)>;

  using list_prompts_handler
      = std::function<result<prompts_list_result, mcp_error> (
          const std::optional<std::string> &cursor)>;

  using get_prompt_handler = std::function<result<prompt_result, mcp_error> (
      const std::string &name,
      const std::optional<std::string> &arguments_json)>;

  using completion_handler
      = std::function<result<completion_result, mcp_error> (
          const completion_params &params)>;

  /**
   * Creates a server with the given identity and capabilities.
   *
   * :param info: Server implementation information.
   * :param caps: Server capabilities.
   */
  explicit mcp_server (implementation_info info, server_capabilities caps);

  mcp_server (const mcp_server &) = delete;
  mcp_server &operator= (const mcp_server &) = delete;

  /**
   * Run the stdio loop: read stdin line by line until EOF.
   *
   * Called by the server process' ``main``; returns when stdin closes
   * or :cpp:func:`request_stop` was called.
   */
  void run ();

  /**
   * Ask the run loop to stop after the current line.
   *
   * Note the loop blocks in a stdin read, so in practice the process
   * exits when the client closes stdin (EOF); this flag covers the
   * case where the server itself decides to shut down.
   */
  void request_stop ();

  /**
   * Handle one newline-delimited message.
   *
   * :param line: One message without the trailing newline.
   * :return: The response line to write to stdout, or nullopt when the
   *   message needs no reply (notifications, or responses to our own
   *   outgoing requests).
   */
  std::optional<std::string> handle_line (const std::string &line);

  // ── Handler registration ─────────────────────────────────────

  void set_list_tools_handler (list_tools_handler handler);
  void set_call_tool_handler (call_tool_handler handler);
  void set_list_resources_handler (list_resources_handler handler);
  void set_list_templates_handler (list_templates_handler handler);
  void set_read_resource_handler (read_resource_handler handler);
  void set_subscribe_handler (subscribe_handler handler);
  void set_unsubscribe_handler (subscribe_handler handler);
  void set_list_prompts_handler (list_prompts_handler handler);
  void set_get_prompt_handler (get_prompt_handler handler);
  void set_completion_handler (completion_handler handler);

  /**
   * Set the handler for notifications/roots/list_changed from the client.
   */
  void set_roots_changed_handler (std::function<void ()> handler);

  /**
   * Set optional instructions advertised during initialize.
   */
  void set_instructions (const std::string &instructions);

  // ── Server -> client requests ────────────────────────────────

  /**
   * Request an LLM sampling from the client.
   */
  result<sampling_result, mcp_error>
  request_sampling (const sampling_params &params,
                    std::chrono::milliseconds timeout
                    = std::chrono::seconds{ 30 });

  /**
   * Request the client's filesystem roots.
   */
  result<roots_list_result, mcp_error>
  request_roots (std::chrono::milliseconds timeout
                 = std::chrono::seconds{ 30 });

  /**
   * Request structured information from the user via the client.
   */
  result<elicitation_result, mcp_error>
  request_elicitation (const elicitation_params &params,
                       std::chrono::milliseconds timeout
                       = std::chrono::seconds{ 30 });

  // ── Server -> client notifications ───────────────────────────

  /**
   * Emit a log message, subject to the client's log-level threshold.
   */
  void emit_log (log_level level, const std::string &data_json,
                 const std::optional<std::string> &logger = std::nullopt);

  /** Notify that the tools list changed. */
  void notify_tools_changed ();

  /** Notify that the resources list changed. */
  void notify_resources_changed ();

  /** Notify that a resource's contents changed. */
  void notify_resource_updated (const std::string &uri);

  /** Notify that the prompts list changed. */
  void notify_prompts_changed ();

  /** Send a progress notification. */
  void notify_progress (const progress_notification &n);

  // ── Introspection ────────────────────────────────────────────

  /** Negotiated protocol version (empty before initialize). */
  std::string protocol_version () const;

  /** Client capabilities from the handshake. */
  client_capabilities client_caps () const;

  /** Client implementation info from the handshake. */
  implementation_info client_info () const;

private:
  struct pending_request
  {
    std::string result;
    std::string error;
    bool completed = false;
  };

  std::optional<std::string> handle_request (const std::string &id_json,
                                             const std::string &method,
                                             const std::string &params);
  void handle_notification_msg (const std::string &method,
                                const std::string &params);
  void complete_outgoing (const std::string &line);

  result<std::string, mcp_error>
  send_request (const std::string &method, const std::string &params_json,
                std::chrono::milliseconds timeout);

  void send_line (const std::string &line);

  mutable std::mutex m_state_mutex;
  implementation_info m_info;
  server_capabilities m_caps;
  std::optional<std::string> m_instructions;

  std::string m_protocol_version;
  client_capabilities m_client_caps;
  implementation_info m_client_info;
  bool m_got_initialize = false;
  bool m_operation = false;
  log_level m_log_threshold = log_level::debug;

  std::mutex m_handler_mutex;
  list_tools_handler m_on_list_tools;
  call_tool_handler m_on_call_tool;
  list_resources_handler m_on_list_resources;
  list_templates_handler m_on_list_templates;
  read_resource_handler m_on_read_resource;
  subscribe_handler m_on_subscribe;
  subscribe_handler m_on_unsubscribe;
  list_prompts_handler m_on_list_prompts;
  get_prompt_handler m_on_get_prompt;
  completion_handler m_on_complete;
  std::function<void ()> m_on_roots_changed;

  std::atomic<bool> m_running{ true };
  std::mutex m_write_mutex;

  std::mutex m_pending_mutex;
  std::unordered_map<int64_t, std::shared_ptr<pending_request>> m_pending;
  std::atomic<int64_t> m_next_id{ 1 };
};

} // namespace agentsdk::mcp
