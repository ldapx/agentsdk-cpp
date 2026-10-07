#pragma once

#include <cstdint>
#include <optional>
#include <string>

namespace agentsdk::a2a
{

/**
 * A2A protocol error codes.
 *
 * Maps to JSON-RPC error codes for the JSON-RPC binding,
 * and to HTTP status codes for the REST binding.
 */
enum class error_code : int32_t
{
  /** The specified task ID does not exist or is not accessible. */
  task_not_found = -32001,
  /** Attempted to cancel a task that is not in a cancelable state. */
  task_not_cancelable = -32002,
  /** Push notification features are not supported by the agent. */
  push_notification_not_supported = -32003,
  /** The requested operation is not supported. */
  unsupported_operation = -32004,
  /** A media type in the request is not supported. */
  content_type_not_supported = -32005,
  /** The agent returned a response that does not conform to the spec. */
  invalid_agent_response = -32006,
  /** Extended agent card is declared but not configured. */
  extended_agent_card_not_configured = -32007,
  /** Server requires an extension the client did not declare. */
  extension_support_required = -32008,
  /** The requested A2A protocol version is not supported. */
  version_not_supported = -32009,

  // Standard JSON-RPC errors.
  /** Invalid JSON payload. */
  parse_error = -32700,
  /** Request payload validation error. */
  invalid_request = -32600,
  /** The requested method does not exist. */
  method_not_found = -32601,
  /** Invalid parameters for the method. */
  invalid_params = -32602,
  /** Internal server error. */
  internal_error = -32603
};

/**
 * Returns a human-readable name for the given error code.
 *
 * :param code: The error code.
 * :return: A short string identifier (e.g. "TaskNotFoundError").
 */
const char *error_code_name (error_code code);

/**
 * Returns the HTTP status code that maps to the given A2A error code.
 *
 * :param code: The A2A error code.
 * :return: HTTP status code (e.g. 404 for task_not_found).
 */
int32_t error_code_http_status (error_code code);

/**
 * An error produced by A2A operations.
 */
struct a2a_error
{
  /** Machine-readable error code. */
  error_code code;
  /** Human-readable error description. */
  std::string message;
  /** Optional structured error details as a raw JSON string. */
  std::optional<std::string> details;
};

} // namespace agentsdk::a2a
