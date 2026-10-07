#pragma once

#include <agentsdk/a2a/result.hpp>
#include <string>

#include <simdjson.h>

#include <agentsdk/a2a/agent_card.hpp>
#include <agentsdk/a2a/errors.hpp>
#include <agentsdk/a2a/request_response.hpp>
#include <agentsdk/a2a/types.hpp>
#include <agentsdk/common/json_builder.hpp>

namespace agentsdk::a2a
{

// Note: the JSON builder lives in the protocol-neutral `agentsdk`
// namespace (<agentsdk/common/json_builder.hpp>); unqualified
// `json_builder` below resolves there.  There is deliberately no
// `a2a::json_builder` alias: it made `json_builder` ambiguous for
// consumers using both `agentsdk` and `agentsdk::a2a` namespaces.

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
