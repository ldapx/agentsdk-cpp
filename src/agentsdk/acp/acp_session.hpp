#pragma once

#include <agentsdk/acp/acp_client.hpp>
#include <agentsdk/acp/acp_types.hpp>

#include <functional>
#include <string>

namespace agentsdk::acp
{

/**
 * ACP v1 session lifecycle.
 *
 * Wraps the ACP session methods with a typed C++ API.  Handles
 * initialization, session creation, prompting, and cancellation.
 */
class acp_session
{
public:
  /**
   * Callback for session/update notifications from the agent.
   *
   * :param method: The notification method (always "session/update").
   * :param params: Raw JSON params of the notification.
   */
  using update_handler = std::function<void (const std::string &method,
                                             const std::string &params)>;

  /**
   * Callback for agent-to-client requests (fs, terminal, permission).
   *
   * Return a handled :cpp:struct:`agent_response` to answer the
   * request from the application layer; returning an unhandled
   * response falls back to the session's built-in protocol handlers.
   */
  using agent_request_handler = std::function<agent_response (
      const std::string &method, const std::string &params)>;

  explicit acp_session (acp_client &client);

  /**
   * Initialize the ACP connection.
   *
   * Negotiates protocol version and exchanges capabilities.
   * Must be called before creating sessions.
   *
   * :param client_caps: Capabilities this client supports.
   * :param info: Client implementation info.
   * :return: true if initialization succeeded.
   */
  bool initialize (const client_capabilities &client_caps,
                   const implementation_info &info);

  /**
   * Create a new session.
   *
   * :param cwd: Working directory for the session.
   * :return: Session ID, or empty on failure.
   */
  std::string new_session (const std::string &cwd);

  /**
   * Send a user prompt as text content.
   *
   * :param text: The user's message.
   * :return: true if the prompt was accepted.
   */
  bool prompt (const std::string &text);

  /**
   * Send a user prompt with explicit content blocks.
   *
   * :param content: Content blocks for the prompt.
   * :return: true if the prompt was accepted.
   */
  bool prompt_with_content (const std::vector<content_block> &content);

  /**
   * Cancel the current prompt turn.
   */
  void cancel ();

  /**
   * Close the current session.
   */
  void close_session ();

  /**
   * Set a configuration option value.
   *
   * :param config_id: The config option id (e.g. "model").
   * :param value: The new value to set.
   * :return: The updated config options, or empty on failure.
   */
  std::vector<config_option> set_config_option (const std::string &config_id,
                                                const std::string &value);

  /**
   * Set handler for session/update notifications.
   */
  void set_update_handler (update_handler handler);

  /**
   * Set handler for agent-to-client requests (fs, terminal, permission).
   */
  void set_agent_request_handler (agent_request_handler handler);

  /** Current session ID. */
  const std::string &session_id () const;

  /** Agent capabilities after initialization. */
  const agent_capabilities &agent_caps () const;

  /** Config options from the agent (available after session/new). */
  const std::vector<config_option> &config_options () const;

  /** Whether the session is initialized. */
  bool is_initialized () const;

  /** Whether a prompt is currently being processed. */
  bool is_processing () const;

private:
  void handle_notification (const std::string &method,
                            const std::string &params);

  agent_response handle_agent_request (const std::string &method,
                                       const std::string &params);

  agent_response handle_permission_request (const std::string &params);

  agent_response handle_fs_read_text_file (const std::string &params);

  agent_response handle_fs_write_text_file (const std::string &params);

  acp_client &m_client;
  bool m_initialized = false;
  std::string m_session_id;
  agent_capabilities m_agent_caps;
  std::vector<config_option> m_config_options;
  bool m_processing = false;

  update_handler m_on_update;
  agent_request_handler m_on_agent_request;
};

} // namespace agentsdk::acp
