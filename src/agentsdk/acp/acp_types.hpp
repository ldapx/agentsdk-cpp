#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace agentsdk::acp
{

/**
 * ACP v1 protocol version.
 */
constexpr int ACP_PROTOCOL_VERSION = 1;

// ── Content blocks ───────────────────────────────────────────────

/**
 * Text content block.  May be plain text or Markdown.
 */
struct text_content
{
  std::string text;
};

/**
 * Image content block (base64-encoded).
 */
struct image_content
{
  std::string data;
  std::string mime_type;
  std::optional<std::string> uri;
};

/**
 * Audio content block (base64-encoded).
 */
struct audio_content
{
  std::string data;
  std::string mime_type;
};

/**
 * Link to an external resource.
 */
struct resource_link_content
{
  std::string name;
  std::string uri;
  std::optional<std::string> mime_type;
  std::optional<std::string> description;
  std::optional<std::string> title;
  std::optional<int64_t> size;
};

/**
 * Embedded resource content (text or binary).
 */
struct embedded_resource_content
{
  struct text_resource
  {
    std::string text;
    std::string uri;
    std::optional<std::string> mime_type;
  };

  struct blob_resource
  {
    std::string blob;
    std::string uri;
    std::optional<std::string> mime_type;
  };

  std::variant<text_resource, blob_resource> resource;
};

/**
 * A content block in the ACP protocol.
 *
 * Used in prompts, tool call results, and streaming updates.
 */
using content_block
    = std::variant<text_content, image_content, audio_content,
                   resource_link_content, embedded_resource_content>;

// ── Tool calls ───────────────────────────────────────────────────

/**
 * Categories of tools that can be invoked.
 */
enum class tool_kind
{
  read,
  edit,
  del,
  move,
  search,
  execute,
  think,
  fetch,
  switch_mode,
  other
};

/**
 * Execution status of a tool call.
 */
enum class tool_call_status
{
  pending,
  in_progress,
  completed,
  failed
};

/**
 * A file location being accessed or modified by a tool.
 */
struct tool_call_location
{
  std::string path;
  std::optional<uint32_t> line;
};

/**
 * Content produced by a tool call.
 */
struct tool_call_content_item
{
  struct content_wrapper
  {
    content_block content;
  };

  struct diff_content
  {
    std::string path;
    std::optional<std::string> old_text;
    std::string new_text;
  };

  struct terminal_content
  {
    std::string terminal_id;
  };

  std::variant<content_wrapper, diff_content, terminal_content> item;
};

/**
 * An update to an existing tool call.
 */
struct tool_call_update
{
  std::string tool_call_id;
  std::optional<tool_kind> kind;
  std::optional<tool_call_status> status;
  std::optional<std::string> title;
  std::optional<std::vector<tool_call_content_item>> content;
  std::optional<std::vector<tool_call_location>> locations;
};

// ── Permission ───────────────────────────────────────────────────

/**
 * The type of permission option.
 */
enum class permission_option_kind
{
  allow_once,
  allow_always,
  reject_once,
  reject_always
};

/**
 * An option presented to the user when requesting permission.
 */
struct permission_option
{
  std::string option_id;
  std::string name;
  permission_option_kind kind;
};

// ── Session config options ──────────────────────────────────────

/**
 * Semantic category for a config option.
 */
enum class config_option_category
{
  mode,
  model,
  model_config,
  thought_level,
  custom
};

/**
 * Type of input control for a config option.
 */
enum class config_option_type
{
  select,
  boolean
};

/**
 * A single selectable value for a config option.
 */
struct config_option_value
{
  std::string value;
  std::string name;
  std::string description;
};

/**
 * A configuration option exposed by the agent.
 *
 * Agents may provide config options during session/new to allow
 * clients to offer selectors for models, modes, etc.
 */
struct config_option
{
  std::string id;
  std::string name;
  std::string description;
  config_option_category category = config_option_category::custom;
  config_option_type type = config_option_type::select;
  std::string current_value;
  std::vector<config_option_value> options;
};

// ── Session updates ──────────────────────────────────────────────

/**
 * An entry in an agent's execution plan.
 */
struct plan_entry
{
  std::string content;
  std::string priority;
  std::string status;
};

/**
 * Token usage and cost information.
 */
struct usage_update
{
  int64_t used = 0;
  int64_t size = 0;
  std::optional<double> cost_amount;
  std::optional<std::string> cost_currency;
};

// ── Initialize ───────────────────────────────────────────────────

/**
 * File system capabilities advertised by the client.
 */
struct client_fs_capabilities
{
  bool read_text_file = false;
  bool write_text_file = false;
};

/**
 * Capabilities advertised by the client during initialization.
 */
struct client_capabilities
{
  client_fs_capabilities fs;
  bool terminal = false;
};

/**
 * Prompt content capabilities advertised by the agent.
 */
struct prompt_capabilities
{
  bool image = false;
  bool audio = false;
  bool embedded_context = false;
};

/**
 * Capabilities advertised by the agent during initialization.
 */
struct agent_capabilities
{
  bool load_session = false;
  prompt_capabilities prompt_caps;
};

/**
 * Implementation information (name, title, version).
 */
struct implementation_info
{
  std::string name;
  std::string title;
  std::string version;
};

// ── JSON-RPC wire format ─────────────────────────────────────────

/**
 * A JSON-RPC 2.0 request id.
 */
using request_id = std::variant<std::nullptr_t, int64_t, std::string>;

/**
 * The reply a client produces for an agent-to-client request
 * (permission prompts, filesystem access, terminal, ...).
 */
struct agent_response
{
  /** Whether the request was recognized; ``false`` replies with -32601. */
  bool handled = false;

  /** Whether the request succeeded; ``false`` replies with an error. */
  bool ok = true;

  /** Result JSON payload (used when handled && ok). */
  std::string result_json = "{}";

  /** JSON-RPC error code (used when handled && !ok). */
  int64_t error_code = -32000;

  /** Human-readable error message (used when handled && !ok). */
  std::string error_message;
};

/**
 * A JSON-RPC 2.0 error object.
 */
struct jsonrpc_error
{
  int64_t code = 0;
  std::string message;
  std::optional<std::string> data;
};

/**
 * Parsed JSON-RPC message envelope.
 */
struct jsonrpc_message
{
  request_id id;
  std::optional<std::string> method;
  std::optional<std::string> params_json;
  std::optional<std::string> result_json;
  std::optional<jsonrpc_error> error;
  bool is_notification = false;
};

// ── Utility ──────────────────────────────────────────────────────

/**
 * Convert tool_kind to string for display.
 */
const char *tool_kind_name (tool_kind kind);

/**
 * Convert tool_call_status to string.
 */
const char *tool_call_status_name (tool_call_status status);

/**
 * Convert permission_option_kind to string.
 */
const char *permission_option_kind_name (permission_option_kind kind);

/**
 * Convert tool_kind from string (for parsing).
 */
std::optional<tool_kind> parse_tool_kind (const std::string &str);

/**
 * Convert tool_call_status from string.
 */
std::optional<tool_call_status> parse_tool_call_status (const std::string &str);

/**
 * Convert config_option_category from string.
 */
config_option_category parse_config_option_category (const std::string &str);

/**
 * Convert config_option_type from string.
 */
config_option_type parse_config_option_type (const std::string &str);

} // namespace agentsdk::acp
