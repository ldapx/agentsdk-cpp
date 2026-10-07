#pragma once

#include <agentsdk/common/result.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace agentsdk::mcp
{

/**
 * Latest MCP protocol version this SDK speaks.
 */
inline constexpr const char *MCP_PROTOCOL_VERSION = "2025-06-18";

// Standard JSON-RPC error codes plus the MCP-specific range.
inline constexpr int64_t PARSE_ERROR = -32700;
inline constexpr int64_t INVALID_REQUEST = -32600;
inline constexpr int64_t METHOD_NOT_FOUND = -32601;
inline constexpr int64_t INVALID_PARAMS = -32602;
inline constexpr int64_t INTERNAL_ERROR = -32603;
inline constexpr int64_t RESOURCE_NOT_FOUND = -32002;

/**
 * An error produced by MCP operations.
 */
struct mcp_error
{
  /** Machine-readable error code. */
  int64_t code = INTERNAL_ERROR;
  /** Human-readable error description. */
  std::string message;
  /** Optional structured error details as a raw JSON string. */
  std::optional<std::string> data;
};

/**
 * Implementation information (name, title, version).
 */
struct implementation_info
{
  std::string name;
  std::optional<std::string> title;
  std::string version;
};

// ── Capabilities ─────────────────────────────────────────────────

/**
 * Client capability: filesystem roots.
 */
struct roots_capability
{
  bool list_changed = false;
};

/**
 * Capabilities advertised by the client during initialization.
 */
struct client_capabilities
{
  std::optional<roots_capability> roots;
  bool sampling = false;
  bool elicitation = false;
};

/**
 * Server tools capability.
 */
struct tools_capability
{
  bool list_changed = false;
};

/**
 * Server resources capability.
 */
struct resources_capability
{
  bool subscribe = false;
  bool list_changed = false;
};

/**
 * Server prompts capability.
 */
struct prompts_capability
{
  bool list_changed = false;
};

/**
 * Capabilities advertised by the server during initialization.
 */
struct server_capabilities
{
  std::optional<tools_capability> tools;
  std::optional<resources_capability> resources;
  std::optional<prompts_capability> prompts;
  bool logging = false;
  bool completions = false;
};

/**
 * Result of the initialize handshake (server side of the exchange).
 */
struct initialize_result
{
  std::string protocol_version;
  server_capabilities capabilities;
  implementation_info server_info;
  std::optional<std::string> instructions;
};

// ── Annotations ──────────────────────────────────────────────────

/**
 * Hints about how a content block or resource should be used or displayed.
 */
struct annotations
{
  std::vector<std::string> audience;
  std::optional<double> priority;
  std::optional<std::string> last_modified;
};

// ── Content blocks ───────────────────────────────────────────────

/**
 * Plain text content.
 */
struct text_content
{
  std::string text;
  std::optional<annotations> annot;
};

/**
 * Base64-encoded image content.
 */
struct image_content
{
  std::string data;
  std::string mime_type;
  std::optional<annotations> annot;
};

/**
 * Base64-encoded audio content.
 */
struct audio_content
{
  std::string data;
  std::string mime_type;
  std::optional<annotations> annot;
};

/**
 * A link to a resource.
 */
struct resource_link
{
  std::string uri;
  std::string name;
  std::optional<std::string> title;
  std::optional<std::string> description;
  std::optional<std::string> mime_type;
  std::optional<annotations> annot;
};

/**
 * A resource embedded inline.
 */
struct embedded_resource
{
  std::string uri;
  std::optional<std::string> mime_type;
  /** Text content; exactly one of text/blob is set. */
  std::optional<std::string> text;
  /** Base64-encoded binary content; exactly one of text/blob is set. */
  std::optional<std::string> blob;
  std::optional<annotations> annot;
};

/**
 * Any content block that can appear in tool results, prompts and sampling.
 */
using content_block = std::variant<text_content, image_content, audio_content,
                                   resource_link, embedded_resource>;

// ── Tools ────────────────────────────────────────────────────────

/**
 * A tool exposed by the server.
 */
struct tool
{
  std::string name;
  std::optional<std::string> title;
  std::optional<std::string> description;
  /** JSON Schema (raw JSON) describing the expected arguments. */
  std::string input_schema = "{}";
  /** Optional JSON Schema (raw JSON) for structured output. */
  std::optional<std::string> output_schema;
  /** Optional tool behaviour hints as raw JSON. */
  std::optional<std::string> annotations_json;
};

/**
 * Result of a tools/list request (one page).
 */
struct tools_list_result
{
  std::vector<tool> tools;
  std::optional<std::string> next_cursor;
};

/**
 * Result of a tools/call request.
 */
struct tool_result
{
  std::vector<content_block> content;
  /** Structured output as raw JSON. */
  std::optional<std::string> structured_content;
  bool is_error = false;
};

// ── Resources ────────────────────────────────────────────────────

/**
 * A resource exposed by the server.
 */
struct resource
{
  std::string uri;
  std::string name;
  std::optional<std::string> title;
  std::optional<std::string> description;
  std::optional<std::string> mime_type;
  std::optional<int64_t> size;
  std::optional<annotations> annot;
};

/**
 * A parameterized resource template.
 */
struct resource_template
{
  std::string uri_template;
  std::string name;
  std::optional<std::string> title;
  std::optional<std::string> description;
  std::optional<std::string> mime_type;
  std::optional<annotations> annot;
};

/**
 * Result of a resources/list request (one page).
 */
struct resources_list_result
{
  std::vector<resource> resources;
  std::optional<std::string> next_cursor;
};

/**
 * Result of a resources/templates/list request (one page).
 */
struct resource_templates_result
{
  std::vector<resource_template> templates;
  std::optional<std::string> next_cursor;
};

/**
 * A single resource payload returned by resources/read.
 */
struct resource_contents
{
  std::string uri;
  std::optional<std::string> mime_type;
  std::optional<std::string> text;
  std::optional<std::string> blob;
};

/**
 * Result of a resources/read request.
 */
struct resource_read_result
{
  std::vector<resource_contents> contents;
};

// ── Prompts ──────────────────────────────────────────────────────

/**
 * A single argument accepted by a prompt.
 */
struct prompt_argument
{
  std::string name;
  std::optional<std::string> title;
  std::optional<std::string> description;
  bool required = false;
};

/**
 * A prompt template exposed by the server.
 */
struct prompt
{
  std::string name;
  std::optional<std::string> title;
  std::optional<std::string> description;
  std::vector<prompt_argument> arguments;
};

/**
 * Result of a prompts/list request (one page).
 */
struct prompts_list_result
{
  std::vector<prompt> prompts;
  std::optional<std::string> next_cursor;
};

/**
 * A message in a prompt.
 */
struct prompt_message
{
  /** Either "user" or "assistant". */
  std::string role;
  content_block content;
};

/**
 * Result of a prompts/get request.
 */
struct prompt_result
{
  std::optional<std::string> description;
  std::vector<prompt_message> messages;
};

// ── Sampling (server -> client) ──────────────────────────────────

/**
 * A message in a sampling request.
 */
struct sampling_message
{
  /** Either "user" or "assistant". */
  std::string role;
  content_block content;
};

/**
 * A model hint for sampling.
 */
struct model_hint
{
  std::optional<std::string> name;
};

/**
 * Model selection preferences for sampling.
 */
struct model_preferences
{
  std::vector<model_hint> hints;
  std::optional<double> cost_priority;
  std::optional<double> speed_priority;
  std::optional<double> intelligence_priority;
};

/**
 * Parameters for a sampling/createMessage request.
 */
struct sampling_params
{
  std::vector<sampling_message> messages;
  std::optional<model_preferences> preferences;
  std::optional<std::string> system_prompt;
  /** "none", "thisServer" or "allServers". */
  std::optional<std::string> include_context;
  std::optional<double> temperature;
  int64_t max_tokens = 0;
  std::vector<std::string> stop_sequences;
  /** Arbitrary metadata as raw JSON. */
  std::optional<std::string> metadata;
};

/**
 * Result of a sampling/createMessage request.
 */
struct sampling_result
{
  std::string role;
  content_block content;
  std::string model;
  std::optional<std::string> stop_reason;
};

// ── Roots (server -> client) ─────────────────────────────────────

/**
 * A filesystem root exposed by the client.
 */
struct root
{
  std::string uri;
  std::optional<std::string> name;
};

/**
 * Result of a roots/list request.
 */
struct roots_list_result
{
  std::vector<root> roots;
};

// ── Elicitation (server -> client) ───────────────────────────────

/**
 * Parameters for an elicitation/create request.
 */
struct elicitation_params
{
  std::string message;
  /** Restricted JSON Schema (raw JSON) for the expected response. */
  std::string requested_schema = "{}";
};

/**
 * Result of an elicitation/create request.
 */
struct elicitation_result
{
  /** "accept", "decline" or "cancel". */
  std::string action;
  /** Submitted data as raw JSON (present when action == "accept"). */
  std::optional<std::string> content;
};

// ── Logging ──────────────────────────────────────────────────────

/**
 * MCP log severity (syslog levels per RFC 5424).
 */
enum class log_level
{
  debug,
  info,
  notice,
  warning,
  error,
  critical,
  alert,
  emergency
};

/**
 * A log message sent from server to client.
 */
struct log_message
{
  log_level level = log_level::info;
  std::optional<std::string> logger;
  /** Arbitrary JSON-serializable payload as raw JSON. */
  std::string data = "null";
};

// ── Completion ───────────────────────────────────────────────────

/**
 * What is being completed: a prompt argument or a resource template.
 */
struct completion_ref
{
  /** "ref/prompt" or "ref/resource". */
  std::string type;
  std::optional<std::string> name;
  std::optional<std::string> uri;
};

/**
 * Parameters for a completion/complete request.
 */
struct completion_params
{
  completion_ref ref;
  std::string argument_name;
  std::string argument_value;
  /** Already-resolved argument names to values as raw JSON object. */
  std::optional<std::string> context_arguments;
};

/**
 * Result of a completion/complete request.
 */
struct completion_result
{
  std::vector<std::string> values;
  std::optional<int64_t> total;
  bool has_more = false;
};

// ── Progress / cancellation ──────────────────────────────────────

/**
 * A progress token: either a string or an integer.
 */
using progress_token = std::variant<std::string, int64_t>;

/**
 * A progress notification.
 */
struct progress_notification
{
  progress_token token;
  double progress = 0.0;
  std::optional<double> total;
  std::optional<std::string> message;
};

/**
 * A request id as it appears on the wire (string or integer).
 */
using wire_id = std::variant<std::string, int64_t>;

// ── Utility ──────────────────────────────────────────────────────

/**
 * Convert a log level to its wire string.
 */
const char *log_level_name (log_level level);

/**
 * Parse a log level from its wire string.
 */
std::optional<log_level> parse_log_level (const std::string &str);

/**
 * Compare log severities: ``true`` if ``a`` is at least as severe as ``b``.
 */
bool log_level_at_least (log_level a, log_level b);

} // namespace agentsdk::mcp
