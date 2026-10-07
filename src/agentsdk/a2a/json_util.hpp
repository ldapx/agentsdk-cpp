#pragma once

#include <agentsdk/a2a/result.hpp>
#include <string>

#include <simdjson.h>

#include <agentsdk/a2a/agent_card.hpp>
#include <agentsdk/a2a/errors.hpp>
#include <agentsdk/a2a/request_response.hpp>
#include <agentsdk/a2a/types.hpp>

namespace agentsdk::a2a
{

/**
 * Lightweight JSON object builder.
 *
 * Produces compact JSON strings for serializing A2A types.
 * Manages nesting via an internal scope stack.
 */
class json_builder
{
public:
  json_builder ();

  json_builder (const json_builder &) = delete;
  json_builder &operator= (const json_builder &) = delete;

  /** Begin a new JSON object. */
  void begin_object ();
  /** Begin a new JSON object with a key (for nesting). */
  void begin_object (const std::string &key);
  /** End the current JSON object. */
  void end_object ();

  /** Begin a new JSON array. */
  void begin_array ();
  /** Begin a new JSON array with a key (for nesting). */
  void begin_array (const std::string &key);
  /** End the current JSON array. */
  void end_array ();

  /** Add a key-value pair with a string value. */
  void add_string (const std::string &key, const std::string &value);
  /** Add an optional string value (omitted if empty). */
  void add_optional_string (const std::string &key,
                            const std::optional<std::string> &value);
  /** Add a key-value pair with an integer value. */
  void add_int (const std::string &key, int64_t value);
  /** Add a key-value pair with a boolean value. */
  void add_bool (const std::string &key, bool value);
  /** Add a raw JSON string as a value. */
  void add_raw_json (const std::string &key, const std::string &raw_json);

  /** Add a string element to the current array. */
  void add_array_string (const std::string &value);
  /** Add an integer element to the current array. */
  void add_array_int (int64_t value);
  /** Add a raw JSON element to the current array. */
  void add_array_raw_json (const std::string &raw_json);

  /** Get the accumulated JSON string. */
  std::string str () const;

private:
  void append_key (const std::string &key);

  std::string m_buffer;
  /** Tracks scope types: ``true`` = object, ``false`` = array. */
  std::vector<bool> m_scopes;
  bool m_first_in_scope = true;
};

// ---------------------------------------------------------------------------
// simdjson parsing helpers
// ---------------------------------------------------------------------------

/** Parse a JSON string into a simdjson DOM element. */
result<simdjson::dom::element, a2a_error> parse_json (const std::string &json);

// ---------------------------------------------------------------------------
// Deserialize from simdjson elements
// ---------------------------------------------------------------------------

result<task_state, a2a_error>
task_state_from_json (simdjson::dom::element element);

result<role, a2a_error> role_from_json (simdjson::dom::element element);

result<part, a2a_error> part_from_json (simdjson::dom::element element);

result<message, a2a_error> message_from_json (simdjson::dom::element element);

result<task_status, a2a_error>
task_status_from_json (simdjson::dom::element element);

result<artifact, a2a_error> artifact_from_json (simdjson::dom::element element);

result<task, a2a_error> task_from_json (simdjson::dom::element element);

result<task_status_update_event, a2a_error>
task_status_update_event_from_json (simdjson::dom::element element);

result<task_artifact_update_event, a2a_error>
task_artifact_update_event_from_json (simdjson::dom::element element);

result<stream_response, a2a_error>
stream_response_from_json (simdjson::dom::element element);

result<agent_card, a2a_error>
agent_card_from_json (simdjson::dom::element element);

result<agent_provider, a2a_error>
agent_provider_from_json (simdjson::dom::element element);

result<agent_capabilities, a2a_error>
agent_capabilities_from_json (simdjson::dom::element element);

result<agent_skill, a2a_error>
agent_skill_from_json (simdjson::dom::element element);

result<agent_interface, a2a_error>
agent_interface_from_json (simdjson::dom::element element);

result<send_message_request, a2a_error>
send_message_request_from_json (simdjson::dom::element element);

result<send_message_configuration, a2a_error>
send_message_configuration_from_json (simdjson::dom::element element);

result<list_tasks_request, a2a_error>
list_tasks_request_from_json (simdjson::dom::element element);

result<list_tasks_response, a2a_error>
list_tasks_response_from_json (simdjson::dom::element element);

result<cancel_task_request, a2a_error>
cancel_task_request_from_json (simdjson::dom::element element);

result<push_notification_config, a2a_error>
push_notification_config_from_json (simdjson::dom::element element);

// ---------------------------------------------------------------------------
// Serialize to JSON strings
// ---------------------------------------------------------------------------

std::string to_json (task_state state);
std::string to_json (role r);

std::string to_json (const part &p);
std::string to_json (const message &m);
std::string to_json (const task_status &s);
std::string to_json (const artifact &a);
std::string to_json (const task &t);
std::string to_json (const task_status_update_event &e);
std::string to_json (const task_artifact_update_event &e);
std::string to_json (const stream_response &sr);

std::string to_json (const agent_provider &p);
std::string to_json (const agent_capabilities &c);
std::string to_json (const agent_skill &s);
std::string to_json (const agent_interface &i);
std::string to_json (const agent_card &card);

std::string to_json (const send_message_configuration &c);
std::string to_json (const send_message_request &req);
std::string to_json (const list_tasks_response &resp);

} // namespace agentsdk::a2a
