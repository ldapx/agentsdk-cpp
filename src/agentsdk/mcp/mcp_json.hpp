#pragma once

#include <agentsdk/common/result.hpp>
#include <agentsdk/mcp/mcp_types.hpp>

#include <simdjson.h>

#include <string>
#include <vector>

namespace agentsdk::mcp
{

/**
 * Parse a JSON string into a simdjson DOM element.
 *
 * Uses a thread-local parser so concurrent protocol threads are safe.
 */
result<simdjson::dom::element, mcp_error> parse_json (const std::string &json);

// ── Deserialize ──────────────────────────────────────────────────

result<implementation_info, mcp_error>
implementation_info_from_json (simdjson::dom::element element);

result<client_capabilities, mcp_error>
client_capabilities_from_json (simdjson::dom::element element);

result<server_capabilities, mcp_error>
server_capabilities_from_json (simdjson::dom::element element);

result<initialize_result, mcp_error>
initialize_result_from_json (simdjson::dom::element element);

result<annotations, mcp_error>
annotations_from_json (simdjson::dom::element element);

result<content_block, mcp_error>
content_block_from_json (simdjson::dom::element element);

result<tool, mcp_error> tool_from_json (simdjson::dom::element element);

result<tools_list_result, mcp_error>
tools_list_from_json (simdjson::dom::element element);

result<tool_result, mcp_error>
tool_result_from_json (simdjson::dom::element element);

result<resource, mcp_error> resource_from_json (simdjson::dom::element element);

result<resource_template, mcp_error>
resource_template_from_json (simdjson::dom::element element);

result<resources_list_result, mcp_error>
resources_list_from_json (simdjson::dom::element element);

result<resource_templates_result, mcp_error>
resource_templates_from_json (simdjson::dom::element element);

result<resource_contents, mcp_error>
resource_contents_from_json (simdjson::dom::element element);

result<resource_read_result, mcp_error>
resource_read_from_json (simdjson::dom::element element);

result<prompt, mcp_error> prompt_from_json (simdjson::dom::element element);

result<prompts_list_result, mcp_error>
prompts_list_from_json (simdjson::dom::element element);

result<prompt_message, mcp_error>
prompt_message_from_json (simdjson::dom::element element);

result<prompt_result, mcp_error>
prompt_result_from_json (simdjson::dom::element element);

result<sampling_message, mcp_error>
sampling_message_from_json (simdjson::dom::element element);

result<sampling_params, mcp_error>
sampling_params_from_json (simdjson::dom::element element);

result<sampling_result, mcp_error>
sampling_result_from_json (simdjson::dom::element element);

result<roots_list_result, mcp_error>
roots_list_from_json (simdjson::dom::element element);

result<elicitation_params, mcp_error>
elicitation_params_from_json (simdjson::dom::element element);

result<elicitation_result, mcp_error>
elicitation_result_from_json (simdjson::dom::element element);

result<log_message, mcp_error>
log_message_from_json (simdjson::dom::element element);

result<completion_params, mcp_error>
completion_params_from_json (simdjson::dom::element element);

result<completion_result, mcp_error>
completion_result_from_json (simdjson::dom::element element);

result<progress_notification, mcp_error>
progress_notification_from_json (simdjson::dom::element element);

// ── Serialize ────────────────────────────────────────────────────

std::string to_json (const implementation_info &info);
std::string to_json (const client_capabilities &caps);
std::string to_json (const server_capabilities &caps);
std::string to_json (const initialize_result &r);
std::string to_json (const annotations &a);
std::string to_json (const content_block &c);
std::string to_json (const tool &t);
std::string to_json (const tool_result &r);
std::string to_json (const resource &r);
std::string to_json (const resource_template &t);
std::string to_json (const resource_contents &c);
std::string to_json (const prompt &p);
std::string to_json (const prompt_message &m);
std::string to_json (const prompt_result &r);
std::string to_json (const sampling_message &m);
std::string to_json (const sampling_params &p);
std::string to_json (const sampling_result &r);
std::string to_json (const root &r);
std::string to_json (const elicitation_params &p);
std::string to_json (const elicitation_result &r);
std::string to_json (const log_message &m);
std::string to_json (const completion_params &p);
std::string to_json (const completion_result &r);
std::string to_json (const progress_notification &n);
std::string to_json (const progress_token &t);

/**
 * Build the params object for an initialize request.
 */
std::string initialize_params_json (const std::string &protocol_version,
                                    const client_capabilities &caps,
                                    const implementation_info &info);

// ── JSON-RPC envelopes ───────────────────────────────────────────

/**
 * Protocol versions this SDK understands, newest first.
 */
const std::vector<std::string> &supported_protocol_versions ();

/**
 * Whether the given protocol version is supported.
 */
bool is_supported_version (const std::string &version);

/**
 * Build a JSON-RPC success response envelope.
 */
std::string envelope_result (const std::string &id_json,
                             const std::string &result_json);

/**
 * Build a JSON-RPC error response envelope.
 */
std::string envelope_error (const std::string &id_json, int64_t code,
                            const std::string &message);

/**
 * Parse a raw JSON-RPC error object into an mcp_error.
 */
mcp_error parse_rpc_error (const std::string &error_json);

} // namespace agentsdk::mcp
