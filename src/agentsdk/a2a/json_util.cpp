#include <agentsdk/a2a/json_util.hpp>

#include <spdlog/fmt/fmt.h>

namespace agentsdk::a2a
{

// ---------------------------------------------------------------------------
// simdjson globally-once instance
// ---------------------------------------------------------------------------

static simdjson::dom::parser &
global_parser ()
{
  static simdjson::dom::parser instance;
  return instance;
}

// ---------------------------------------------------------------------------
// simdjson parsing
// ---------------------------------------------------------------------------

result<simdjson::dom::element, a2a_error>
parse_json (const std::string &json)
{
  auto parse_result = global_parser ().parse (json);
  if (parse_result.error ()) {
    return a2a_error{ error_code::parse_error,
                      fmt::format (
                          "JSON parse error: {}",
                          simdjson::error_message (parse_result.error ())),
                      {} };
  }
  return parse_result.value ();
}

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------

static result<std::string, a2a_error>
get_string (simdjson::dom::element element, const std::string &field)
{
  auto get_result = element[field].get_string ();
  if (get_result.error ()) {
    return a2a_error{ error_code::invalid_params,
                      fmt::format ("Missing or invalid field: {}", field),
                      {} };
  }
  return std::string (get_result.value ());
}

static std::optional<std::string>
get_optional_string (simdjson::dom::element element, const std::string &field)
{
  auto get_result = element[field].get_string ();
  if (get_result.error ()) {
    return std::nullopt;
  }
  return std::string (get_result.value ());
}

static std::optional<std::string>
get_raw_json (simdjson::dom::element element, const std::string &field)
{
  auto sub = element[field];
  if (sub.error ()) {
    return std::nullopt;
  }
  return simdjson::to_string (sub.value ());
}

// ---------------------------------------------------------------------------
// Deserialize: enums
// ---------------------------------------------------------------------------

result<task_state, a2a_error>
task_state_from_json (simdjson::dom::element element)
{
  auto str_result = element.get_string ();
  if (str_result.error ()) {
    return a2a_error{ error_code::invalid_params,
                      "Expected a string for task_state",
                      {} };
  }
  std::string value (str_result.value ());
  if (value == "TASK_STATE_UNSPECIFIED" || value == "unspecified") {
    return task_state::unspecified;
  }
  if (value == "TASK_STATE_SUBMITTED" || value == "submitted") {
    return task_state::submitted;
  }
  if (value == "TASK_STATE_WORKING" || value == "working") {
    return task_state::working;
  }
  if (value == "TASK_STATE_COMPLETED" || value == "completed") {
    return task_state::completed;
  }
  if (value == "TASK_STATE_FAILED" || value == "failed") {
    return task_state::failed;
  }
  if (value == "TASK_STATE_CANCELED" || value == "canceled") {
    return task_state::canceled;
  }
  if (value == "TASK_STATE_INPUT_REQUIRED" || value == "input_required") {
    return task_state::input_required;
  }
  if (value == "TASK_STATE_REJECTED" || value == "rejected") {
    return task_state::rejected;
  }
  if (value == "TASK_STATE_AUTH_REQUIRED" || value == "auth_required") {
    return task_state::auth_required;
  }
  return a2a_error{ error_code::invalid_params,
                    fmt::format ("Unknown task_state: {}", value),
                    {} };
}

result<role, a2a_error>
role_from_json (simdjson::dom::element element)
{
  auto str_result = element.get_string ();
  if (str_result.error ()) {
    return a2a_error{ error_code::invalid_params,
                      "Expected a string for role",
                      {} };
  }
  std::string value (str_result.value ());
  if (value == "ROLE_UNSPECIFIED" || value == "unspecified") {
    return role::unspecified;
  }
  if (value == "ROLE_USER" || value == "user") {
    return role::user;
  }
  if (value == "ROLE_AGENT" || value == "agent") {
    return role::agent;
  }
  return a2a_error{ error_code::invalid_params,
                    fmt::format ("Unknown role: {}", value),
                    {} };
}

// ---------------------------------------------------------------------------
// Deserialize: core types
// ---------------------------------------------------------------------------

result<part, a2a_error>
part_from_json (simdjson::dom::element element)
{
  part p;
  p.text = get_optional_string (element, "text");
  p.raw = get_optional_string (element, "raw");
  p.url = get_optional_string (element, "url");
  p.data = get_raw_json (element, "data");
  p.metadata = get_raw_json (element, "metadata");
  p.filename = get_optional_string (element, "filename");
  p.media_type = get_optional_string (element, "mediaType");
  return p;
}

result<message, a2a_error>
message_from_json (simdjson::dom::element element)
{
  message m;

  auto id_result = get_string (element, "messageId");
  if (!id_result) {
    return id_result.error ();
  }
  m.message_id = id_result.value ();

  m.context_id = get_optional_string (element, "contextId");
  m.task_id = get_optional_string (element, "taskId");

  auto role_el = element["role"];
  if (role_el.error ()) {
    return a2a_error{ error_code::invalid_params, "Missing role field", {} };
  }
  auto role_val = role_from_json (role_el.value ());
  if (!role_val) {
    return role_val.error ();
  }
  m.role = role_val.value ();

  auto parts_el = element["parts"];
  if (!parts_el.error () && parts_el.value ().is_array ()) {
    for (auto part_elem : parts_el.value ().get_array ()) {
      auto p = part_from_json (part_elem);
      if (p) {
        m.parts.push_back (p.value ());
      }
    }
  }

  m.metadata = get_raw_json (element, "metadata");

  auto extensions_el = element["extensions"];
  if (!extensions_el.error () && extensions_el.value ().is_array ()) {
    for (auto ext : extensions_el.value ().get_array ()) {
      auto s = ext.get_string ();
      if (!s.error ()) {
        m.extensions.push_back (std::string (s.value ()));
      }
    }
  }

  auto ref_ids_el = element["referenceTaskIds"];
  if (!ref_ids_el.error () && ref_ids_el.value ().is_array ()) {
    for (auto ref : ref_ids_el.value ().get_array ()) {
      auto s = ref.get_string ();
      if (!s.error ()) {
        m.reference_task_ids.push_back (std::string (s.value ()));
      }
    }
  }

  return m;
}

result<task_status, a2a_error>
task_status_from_json (simdjson::dom::element element)
{
  task_status ts;

  auto state_el = element["state"];
  if (state_el.error ()) {
    return a2a_error{ error_code::invalid_params, "Missing state field", {} };
  }
  auto state_val = task_state_from_json (state_el.value ());
  if (!state_val) {
    return state_val.error ();
  }
  ts.state = state_val.value ();

  auto msg_el = element["message"];
  if (!msg_el.error ()) {
    auto parsed = message_from_json (msg_el.value ());
    if (parsed) {
      ts.status_message = parsed.value ();
    }
  }

  ts.timestamp = get_optional_string (element, "timestamp");

  return ts;
}

result<artifact, a2a_error>
artifact_from_json (simdjson::dom::element element)
{
  artifact a;

  auto id_result = get_string (element, "artifactId");
  if (!id_result) {
    return id_result.error ();
  }
  a.artifact_id = id_result.value ();

  a.name = get_optional_string (element, "name");
  a.description = get_optional_string (element, "description");

  auto parts_el = element["parts"];
  if (!parts_el.error () && parts_el.value ().is_array ()) {
    for (auto part_elem : parts_el.value ().get_array ()) {
      auto p = part_from_json (part_elem);
      if (p) {
        a.parts.push_back (p.value ());
      }
    }
  }

  a.metadata = get_raw_json (element, "metadata");

  auto extensions_el = element["extensions"];
  if (!extensions_el.error () && extensions_el.value ().is_array ()) {
    for (auto ext : extensions_el.value ().get_array ()) {
      auto s = ext.get_string ();
      if (!s.error ()) {
        a.extensions.push_back (std::string (s.value ()));
      }
    }
  }

  return a;
}

result<task, a2a_error>
task_from_json (simdjson::dom::element element)
{
  task t;

  auto id_result = get_string (element, "id");
  if (!id_result) {
    return id_result.error ();
  }
  t.id = id_result.value ();

  t.context_id = get_optional_string (element, "contextId");

  auto status_el = element["status"];
  if (status_el.error ()) {
    return a2a_error{ error_code::invalid_params, "Missing status field", {} };
  }
  auto status_val = task_status_from_json (status_el.value ());
  if (!status_val) {
    return status_val.error ();
  }
  t.status = status_val.value ();

  auto artifacts_el = element["artifacts"];
  if (!artifacts_el.error () && artifacts_el.value ().is_array ()) {
    for (auto art : artifacts_el.value ().get_array ()) {
      auto a = artifact_from_json (art);
      if (a) {
        t.artifacts.push_back (a.value ());
      }
    }
  }

  auto history_el = element["history"];
  if (!history_el.error () && history_el.value ().is_array ()) {
    for (auto hist : history_el.value ().get_array ()) {
      auto m = message_from_json (hist);
      if (m) {
        t.history.push_back (m.value ());
      }
    }
  }

  t.metadata = get_raw_json (element, "metadata");

  return t;
}

result<task_status_update_event, a2a_error>
task_status_update_event_from_json (simdjson::dom::element element)
{
  task_status_update_event e;

  auto task_id_result = get_string (element, "taskId");
  if (!task_id_result) {
    return task_id_result.error ();
  }
  e.task_id = task_id_result.value ();

  auto ctx_id_result = get_string (element, "contextId");
  if (!ctx_id_result) {
    return ctx_id_result.error ();
  }
  e.context_id = ctx_id_result.value ();

  auto status_el = element["status"];
  if (status_el.error ()) {
    return a2a_error{ error_code::invalid_params, "Missing status field", {} };
  }
  auto status_val = task_status_from_json (status_el.value ());
  if (!status_val) {
    return status_val.error ();
  }
  e.status = status_val.value ();

  e.metadata = get_raw_json (element, "metadata");

  return e;
}

result<task_artifact_update_event, a2a_error>
task_artifact_update_event_from_json (simdjson::dom::element element)
{
  task_artifact_update_event e;

  auto task_id_result = get_string (element, "taskId");
  if (!task_id_result) {
    return task_id_result.error ();
  }
  e.task_id = task_id_result.value ();

  auto ctx_id_result = get_string (element, "contextId");
  if (!ctx_id_result) {
    return ctx_id_result.error ();
  }
  e.context_id = ctx_id_result.value ();

  auto artifact_el = element["artifact"];
  if (artifact_el.error ()) {
    return a2a_error{ error_code::invalid_params,
                      "Missing artifact field",
                      {} };
  }
  auto art_val = artifact_from_json (artifact_el.value ());
  if (!art_val) {
    return art_val.error ();
  }
  e.artifact_data = art_val.value ();

  auto append_el = element["append"];
  if (!append_el.error ()) {
    auto val = append_el.value ().get_bool ();
    if (!val.error ()) {
      e.append = val.value ();
    }
  }

  auto last_chunk_el = element["lastChunk"];
  if (!last_chunk_el.error ()) {
    auto val = last_chunk_el.value ().get_bool ();
    if (!val.error ()) {
      e.last_chunk = val.value ();
    }
  }

  e.metadata = get_raw_json (element, "metadata");

  return e;
}

result<stream_response, a2a_error>
stream_response_from_json (simdjson::dom::element element)
{
  stream_response sr;

  auto task_el = element["task"];
  if (!task_el.error ()) {
    auto t = task_from_json (task_el.value ());
    if (t) {
      sr.task_payload = t.value ();
    }
  }

  auto msg_el = element["message"];
  if (!msg_el.error ()) {
    auto m = message_from_json (msg_el.value ());
    if (m) {
      sr.message_payload = m.value ();
    }
  }

  auto status_el = element["statusUpdate"];
  if (!status_el.error ()) {
    auto e = task_status_update_event_from_json (status_el.value ());
    if (e) {
      sr.status_update = e.value ();
    }
  }

  auto art_el = element["artifactUpdate"];
  if (!art_el.error ()) {
    auto e = task_artifact_update_event_from_json (art_el.value ());
    if (e) {
      sr.artifact_update = e.value ();
    }
  }

  return sr;
}

// ---------------------------------------------------------------------------
// Deserialize: agent card types
// ---------------------------------------------------------------------------

result<agent_provider, a2a_error>
agent_provider_from_json (simdjson::dom::element element)
{
  agent_provider p;

  auto url_result = get_string (element, "url");
  if (!url_result) {
    return url_result.error ();
  }
  p.url = url_result.value ();

  auto org_result = get_string (element, "organization");
  if (!org_result) {
    return org_result.error ();
  }
  p.organization = org_result.value ();

  return p;
}

result<agent_capabilities, a2a_error>
agent_capabilities_from_json (simdjson::dom::element element)
{
  agent_capabilities c;

  auto streaming_el = element["streaming"];
  if (!streaming_el.error ()) {
    auto val = streaming_el.value ().get_bool ();
    if (!val.error ()) {
      c.streaming = val.value ();
    }
  }

  auto push_el = element["pushNotifications"];
  if (!push_el.error ()) {
    auto val = push_el.value ().get_bool ();
    if (!val.error ()) {
      c.push_notifications = val.value ();
    }
  }

  auto ext_el = element["extendedAgentCard"];
  if (!ext_el.error ()) {
    auto val = ext_el.value ().get_bool ();
    if (!val.error ()) {
      c.extended_agent_card = val.value ();
    }
  }

  return c;
}

result<agent_skill, a2a_error>
agent_skill_from_json (simdjson::dom::element element)
{
  agent_skill s;

  auto id_result = get_string (element, "id");
  if (!id_result) {
    return id_result.error ();
  }
  s.id = id_result.value ();

  auto name_result = get_string (element, "name");
  if (!name_result) {
    return name_result.error ();
  }
  s.name = name_result.value ();

  auto desc_result = get_string (element, "description");
  if (!desc_result) {
    return desc_result.error ();
  }
  s.description = desc_result.value ();

  auto tags_el = element["tags"];
  if (!tags_el.error () && tags_el.value ().is_array ()) {
    for (auto tag : tags_el.value ().get_array ()) {
      auto val = tag.get_string ();
      if (!val.error ()) {
        s.tags.push_back (std::string (val.value ()));
      }
    }
  }

  auto examples_el = element["examples"];
  if (!examples_el.error () && examples_el.value ().is_array ()) {
    for (auto ex : examples_el.value ().get_array ()) {
      auto val = ex.get_string ();
      if (!val.error ()) {
        s.examples.push_back (std::string (val.value ()));
      }
    }
  }

  auto input_modes_el = element["inputModes"];
  if (!input_modes_el.error () && input_modes_el.value ().is_array ()) {
    for (auto mode : input_modes_el.value ().get_array ()) {
      auto val = mode.get_string ();
      if (!val.error ()) {
        s.input_modes.push_back (std::string (val.value ()));
      }
    }
  }

  auto output_modes_el = element["outputModes"];
  if (!output_modes_el.error () && output_modes_el.value ().is_array ()) {
    for (auto mode : output_modes_el.value ().get_array ()) {
      auto val = mode.get_string ();
      if (!val.error ()) {
        s.output_modes.push_back (std::string (val.value ()));
      }
    }
  }

  return s;
}

result<agent_interface, a2a_error>
agent_interface_from_json (simdjson::dom::element element)
{
  agent_interface i;

  auto url_result = get_string (element, "url");
  if (!url_result) {
    return url_result.error ();
  }
  i.url = url_result.value ();

  auto binding_result = get_string (element, "protocolBinding");
  if (!binding_result) {
    return binding_result.error ();
  }
  i.protocol_binding = binding_result.value ();

  i.tenant = get_optional_string (element, "tenant");

  auto version_result = get_string (element, "protocolVersion");
  if (version_result) {
    i.protocol_version = version_result.value ();
  }

  return i;
}

result<agent_card, a2a_error>
agent_card_from_json (simdjson::dom::element element)
{
  agent_card card;

  auto name_result = get_string (element, "name");
  if (!name_result) {
    return name_result.error ();
  }
  card.name = name_result.value ();

  auto desc_result = get_string (element, "description");
  if (!desc_result) {
    return desc_result.error ();
  }
  card.description = desc_result.value ();

  auto interfaces_el = element["supportedInterfaces"];
  if (!interfaces_el.error () && interfaces_el.value ().is_array ()) {
    for (auto iface : interfaces_el.value ().get_array ()) {
      auto i = agent_interface_from_json (iface);
      if (i) {
        card.supported_interfaces.push_back (i.value ());
      }
    }
  }

  auto provider_el = element["provider"];
  if (!provider_el.error ()) {
    auto p = agent_provider_from_json (provider_el.value ());
    if (p) {
      card.provider = p.value ();
    }
  }

  auto version_result = get_string (element, "version");
  if (version_result) {
    card.version = version_result.value ();
  }

  card.documentation_url = get_optional_string (element, "documentationUrl");

  auto caps_el = element["capabilities"];
  if (!caps_el.error ()) {
    auto c = agent_capabilities_from_json (caps_el.value ());
    if (c) {
      card.capabilities = c.value ();
    }
  }

  auto input_modes_el = element["defaultInputModes"];
  if (!input_modes_el.error () && input_modes_el.value ().is_array ()) {
    card.default_input_modes.clear ();
    for (auto mode : input_modes_el.value ().get_array ()) {
      auto val = mode.get_string ();
      if (!val.error ()) {
        card.default_input_modes.push_back (std::string (val.value ()));
      }
    }
  }

  auto output_modes_el = element["defaultOutputModes"];
  if (!output_modes_el.error () && output_modes_el.value ().is_array ()) {
    card.default_output_modes.clear ();
    for (auto mode : output_modes_el.value ().get_array ()) {
      auto val = mode.get_string ();
      if (!val.error ()) {
        card.default_output_modes.push_back (std::string (val.value ()));
      }
    }
  }

  auto skills_el = element["skills"];
  if (!skills_el.error () && skills_el.value ().is_array ()) {
    for (auto skill : skills_el.value ().get_array ()) {
      auto s = agent_skill_from_json (skill);
      if (s) {
        card.skills.push_back (s.value ());
      }
    }
  }

  card.icon_url = get_optional_string (element, "iconUrl");

  return card;
}

// ---------------------------------------------------------------------------
// Deserialize: request/response types
// ---------------------------------------------------------------------------

result<send_message_configuration, a2a_error>
send_message_configuration_from_json (simdjson::dom::element element)
{
  send_message_configuration c;

  auto modes_el = element["acceptedOutputModes"];
  if (!modes_el.error () && modes_el.value ().is_array ()) {
    for (auto mode : modes_el.value ().get_array ()) {
      auto val = mode.get_string ();
      if (!val.error ()) {
        c.accepted_output_modes.push_back (std::string (val.value ()));
      }
    }
  }

  auto hist_el = element["historyLength"];
  if (!hist_el.error ()) {
    auto val = hist_el.value ().get_int64 ();
    if (!val.error ()) {
      c.history_length = static_cast<int32_t> (val.value ());
    }
  }

  auto ret_el = element["returnImmediately"];
  if (!ret_el.error ()) {
    auto val = ret_el.value ().get_bool ();
    if (!val.error ()) {
      c.return_immediately = val.value ();
    }
  }

  return c;
}

result<send_message_request, a2a_error>
send_message_request_from_json (simdjson::dom::element element)
{
  send_message_request req;

  auto msg_el = element["message"];
  if (msg_el.error ()) {
    return a2a_error{ error_code::invalid_params, "Missing message field", {} };
  }
  auto msg = message_from_json (msg_el.value ());
  if (!msg) {
    return msg.error ();
  }
  req.msg = msg.value ();

  auto config_el = element["configuration"];
  if (!config_el.error ()) {
    auto c = send_message_configuration_from_json (config_el.value ());
    if (c) {
      req.configuration = c.value ();
    }
  }

  req.metadata = get_raw_json (element, "metadata");

  return req;
}

result<list_tasks_request, a2a_error>
list_tasks_request_from_json (simdjson::dom::element element)
{
  list_tasks_request req;

  req.context_id = get_optional_string (element, "contextId");

  auto status_el = element["status"];
  if (!status_el.error ()) {
    auto s = task_state_from_json (status_el.value ());
    if (s) {
      req.status = s.value ();
    }
  }

  auto page_size_el = element["pageSize"];
  if (!page_size_el.error ()) {
    auto val = page_size_el.value ().get_int64 ();
    if (!val.error ()) {
      req.page_size = static_cast<int32_t> (val.value ());
    }
  }

  req.page_token = get_optional_string (element, "pageToken");

  auto hist_el = element["historyLength"];
  if (!hist_el.error ()) {
    auto val = hist_el.value ().get_int64 ();
    if (!val.error ()) {
      req.history_length = static_cast<int32_t> (val.value ());
    }
  }

  auto incl_el = element["includeArtifacts"];
  if (!incl_el.error ()) {
    auto val = incl_el.value ().get_bool ();
    if (!val.error ()) {
      req.include_artifacts = val.value ();
    }
  }

  return req;
}

result<list_tasks_response, a2a_error>
list_tasks_response_from_json (simdjson::dom::element element)
{
  list_tasks_response resp;

  auto tasks_el = element["tasks"];
  if (!tasks_el.error () && tasks_el.value ().is_array ()) {
    for (auto t : tasks_el.value ().get_array ()) {
      auto parsed = task_from_json (t);
      if (parsed) {
        resp.tasks.push_back (parsed.value ());
      }
    }
  }

  auto next_token_result = get_string (element, "nextPageToken");
  if (next_token_result) {
    resp.next_page_token = next_token_result.value ();
  }

  auto page_size_el = element["pageSize"];
  if (!page_size_el.error ()) {
    auto val = page_size_el.value ().get_int64 ();
    if (!val.error ()) {
      resp.page_size = static_cast<int32_t> (val.value ());
    }
  }

  auto total_el = element["totalSize"];
  if (!total_el.error ()) {
    auto val = total_el.value ().get_int64 ();
    if (!val.error ()) {
      resp.total_size = static_cast<int32_t> (val.value ());
    }
  }

  return resp;
}

result<cancel_task_request, a2a_error>
cancel_task_request_from_json (simdjson::dom::element element)
{
  cancel_task_request req;

  auto id_result = get_string (element, "id");
  if (!id_result) {
    return id_result.error ();
  }
  req.id = id_result.value ();

  req.metadata = get_raw_json (element, "metadata");

  return req;
}

result<push_notification_config, a2a_error>
push_notification_config_from_json (simdjson::dom::element element)
{
  push_notification_config cfg;

  auto id_result = get_string (element, "id");
  if (!id_result) {
    return id_result.error ();
  }
  cfg.id = id_result.value ();

  auto task_id_result = get_string (element, "taskId");
  if (!task_id_result) {
    return task_id_result.error ();
  }
  cfg.task_id = task_id_result.value ();

  auto url_result = get_string (element, "url");
  if (!url_result) {
    return url_result.error ();
  }
  cfg.url = url_result.value ();

  cfg.token = get_optional_string (element, "token");
  cfg.auth_scheme = get_optional_string (element, "authScheme");
  cfg.auth_credentials = get_optional_string (element, "authCredentials");

  return cfg;
}

// ---------------------------------------------------------------------------
// Serialize: enums
// ---------------------------------------------------------------------------

std::string
to_json (task_state state)
{
  switch (state) {
  case task_state::unspecified:
    return "\"TASK_STATE_UNSPECIFIED\"";
  case task_state::submitted:
    return "\"TASK_STATE_SUBMITTED\"";
  case task_state::working:
    return "\"TASK_STATE_WORKING\"";
  case task_state::completed:
    return "\"TASK_STATE_COMPLETED\"";
  case task_state::failed:
    return "\"TASK_STATE_FAILED\"";
  case task_state::canceled:
    return "\"TASK_STATE_CANCELED\"";
  case task_state::input_required:
    return "\"TASK_STATE_INPUT_REQUIRED\"";
  case task_state::rejected:
    return "\"TASK_STATE_REJECTED\"";
  case task_state::auth_required:
    return "\"TASK_STATE_AUTH_REQUIRED\"";
  }
  return "\"TASK_STATE_UNSPECIFIED\"";
}

std::string
to_json (role r)
{
  switch (r) {
  case role::unspecified:
    return "\"ROLE_UNSPECIFIED\"";
  case role::user:
    return "\"ROLE_USER\"";
  case role::agent:
    return "\"ROLE_AGENT\"";
  }
  return "\"ROLE_UNSPECIFIED\"";
}

// ---------------------------------------------------------------------------
// Serialize: core types
// ---------------------------------------------------------------------------

std::string
to_json (const part &p)
{
  json_builder jb;
  jb.begin_object ();
  if (p.text) {
    jb.add_string ("text", *p.text);
  }
  if (p.raw) {
    jb.add_string ("raw", *p.raw);
  }
  if (p.url) {
    jb.add_string ("url", *p.url);
  }
  if (p.data) {
    jb.add_raw_json ("data", *p.data);
  }
  if (p.metadata) {
    jb.add_raw_json ("metadata", *p.metadata);
  }
  if (p.filename) {
    jb.add_string ("filename", *p.filename);
  }
  if (p.media_type) {
    jb.add_string ("mediaType", *p.media_type);
  }
  jb.end_object ();
  return jb.str ();
}

std::string
to_json (const message &m)
{
  json_builder jb;
  jb.begin_object ();
  jb.add_string ("messageId", m.message_id);
  jb.add_optional_string ("contextId", m.context_id);
  jb.add_optional_string ("taskId", m.task_id);
  jb.add_raw_json ("role", to_json (m.role));
  jb.begin_array ();
  for (const part &p : m.parts) {
    jb.add_array_raw_json (to_json (p));
  }
  jb.end_array ();
  if (m.metadata) {
    jb.add_raw_json ("metadata", *m.metadata);
  }
  if (!m.extensions.empty ()) {
    jb.begin_array ();
    for (const auto &ext : m.extensions) {
      jb.add_array_string (ext);
    }
    jb.end_array ();
  }
  if (!m.reference_task_ids.empty ()) {
    jb.begin_array ();
    for (const auto &ref : m.reference_task_ids) {
      jb.add_array_string (ref);
    }
    jb.end_array ();
  }
  jb.end_object ();
  return jb.str ();
}

std::string
to_json (const task_status &s)
{
  json_builder jb;
  jb.begin_object ();
  jb.add_raw_json ("state", to_json (s.state));
  if (s.status_message) {
    jb.add_raw_json ("message", to_json (*s.status_message));
  }
  jb.add_optional_string ("timestamp", s.timestamp);
  jb.end_object ();
  return jb.str ();
}

std::string
to_json (const artifact &a)
{
  json_builder jb;
  jb.begin_object ();
  jb.add_string ("artifactId", a.artifact_id);
  jb.add_optional_string ("name", a.name);
  jb.add_optional_string ("description", a.description);
  jb.begin_array ();
  for (const part &p : a.parts) {
    jb.add_array_raw_json (to_json (p));
  }
  jb.end_array ();
  if (a.metadata) {
    jb.add_raw_json ("metadata", *a.metadata);
  }
  if (!a.extensions.empty ()) {
    jb.begin_array ();
    for (const auto &ext : a.extensions) {
      jb.add_array_string (ext);
    }
    jb.end_array ();
  }
  jb.end_object ();
  return jb.str ();
}

std::string
to_json (const task &t)
{
  json_builder jb;
  jb.begin_object ();
  jb.add_string ("id", t.id);
  jb.add_optional_string ("contextId", t.context_id);
  jb.add_raw_json ("status", to_json (t.status));
  if (!t.artifacts.empty ()) {
    jb.begin_array ();
    for (const artifact &a : t.artifacts) {
      jb.add_array_raw_json (to_json (a));
    }
    jb.end_array ();
  }
  if (!t.history.empty ()) {
    jb.begin_array ();
    for (const message &m : t.history) {
      jb.add_array_raw_json (to_json (m));
    }
    jb.end_array ();
  }
  if (t.metadata) {
    jb.add_raw_json ("metadata", *t.metadata);
  }
  jb.end_object ();
  return jb.str ();
}

std::string
to_json (const task_status_update_event &e)
{
  json_builder jb;
  jb.begin_object ();
  jb.add_string ("taskId", e.task_id);
  jb.add_string ("contextId", e.context_id);
  jb.add_raw_json ("status", to_json (e.status));
  if (e.metadata) {
    jb.add_raw_json ("metadata", *e.metadata);
  }
  jb.end_object ();
  return jb.str ();
}

std::string
to_json (const task_artifact_update_event &e)
{
  json_builder jb;
  jb.begin_object ();
  jb.add_string ("taskId", e.task_id);
  jb.add_string ("contextId", e.context_id);
  jb.add_raw_json ("artifact", to_json (e.artifact_data));
  jb.add_bool ("append", e.append);
  jb.add_bool ("lastChunk", e.last_chunk);
  if (e.metadata) {
    jb.add_raw_json ("metadata", *e.metadata);
  }
  jb.end_object ();
  return jb.str ();
}

std::string
to_json (const stream_response &sr)
{
  json_builder jb;
  jb.begin_object ();
  if (sr.task_payload) {
    jb.add_raw_json ("task", to_json (*sr.task_payload));
  }
  if (sr.message_payload) {
    jb.add_raw_json ("message", to_json (*sr.message_payload));
  }
  if (sr.status_update) {
    jb.add_raw_json ("statusUpdate", to_json (*sr.status_update));
  }
  if (sr.artifact_update) {
    jb.add_raw_json ("artifactUpdate", to_json (*sr.artifact_update));
  }
  jb.end_object ();
  return jb.str ();
}

// ---------------------------------------------------------------------------
// Serialize: agent card types
// ---------------------------------------------------------------------------

std::string
to_json (const agent_provider &p)
{
  json_builder jb;
  jb.begin_object ();
  jb.add_string ("url", p.url);
  jb.add_string ("organization", p.organization);
  jb.end_object ();
  return jb.str ();
}

std::string
to_json (const agent_capabilities &c)
{
  json_builder jb;
  jb.begin_object ();
  jb.add_bool ("streaming", c.streaming);
  jb.add_bool ("pushNotifications", c.push_notifications);
  jb.add_bool ("extendedAgentCard", c.extended_agent_card);
  jb.end_object ();
  return jb.str ();
}

std::string
to_json (const agent_skill &s)
{
  json_builder jb;
  jb.begin_object ();
  jb.add_string ("id", s.id);
  jb.add_string ("name", s.name);
  jb.add_string ("description", s.description);
  jb.begin_array ();
  for (const auto &tag : s.tags) {
    jb.add_array_string (tag);
  }
  jb.end_array ();
  if (!s.examples.empty ()) {
    jb.begin_array ();
    for (const auto &ex : s.examples) {
      jb.add_array_string (ex);
    }
    jb.end_array ();
  }
  if (!s.input_modes.empty ()) {
    jb.begin_array ();
    for (const auto &mode : s.input_modes) {
      jb.add_array_string (mode);
    }
    jb.end_array ();
  }
  if (!s.output_modes.empty ()) {
    jb.begin_array ();
    for (const auto &mode : s.output_modes) {
      jb.add_array_string (mode);
    }
    jb.end_array ();
  }
  jb.end_object ();
  return jb.str ();
}

std::string
to_json (const agent_interface &i)
{
  json_builder jb;
  jb.begin_object ();
  jb.add_string ("url", i.url);
  jb.add_string ("protocolBinding", i.protocol_binding);
  jb.add_optional_string ("tenant", i.tenant);
  jb.add_string ("protocolVersion", i.protocol_version);
  jb.end_object ();
  return jb.str ();
}

std::string
to_json (const agent_card &card)
{
  json_builder jb;
  jb.begin_object ();
  jb.add_string ("name", card.name);
  jb.add_string ("description", card.description);
  jb.begin_array ();
  for (const agent_interface &iface : card.supported_interfaces) {
    jb.add_array_raw_json (to_json (iface));
  }
  jb.end_array ();
  if (card.provider) {
    jb.add_raw_json ("provider", to_json (*card.provider));
  }
  jb.add_string ("version", card.version);
  jb.add_optional_string ("documentationUrl", card.documentation_url);
  jb.add_raw_json ("capabilities", to_json (card.capabilities));
  jb.begin_array ();
  for (const auto &mode : card.default_input_modes) {
    jb.add_array_string (mode);
  }
  jb.end_array ();
  jb.begin_array ();
  for (const auto &mode : card.default_output_modes) {
    jb.add_array_string (mode);
  }
  jb.end_array ();
  jb.begin_array ();
  for (const agent_skill &skill : card.skills) {
    jb.add_array_raw_json (to_json (skill));
  }
  jb.end_array ();
  jb.add_optional_string ("iconUrl", card.icon_url);
  jb.end_object ();
  return jb.str ();
}

// ---------------------------------------------------------------------------
// Serialize: request/response types
// ---------------------------------------------------------------------------

std::string
to_json (const send_message_configuration &c)
{
  json_builder jb;
  jb.begin_object ();
  if (!c.accepted_output_modes.empty ()) {
    jb.begin_array ();
    for (const auto &mode : c.accepted_output_modes) {
      jb.add_array_string (mode);
    }
    jb.end_array ();
  }
  if (c.history_length) {
    jb.add_int ("historyLength", *c.history_length);
  }
  jb.add_bool ("returnImmediately", c.return_immediately);
  jb.end_object ();
  return jb.str ();
}

std::string
to_json (const send_message_request &req)
{
  json_builder jb;
  jb.begin_object ();
  jb.add_raw_json ("message", to_json (req.msg));
  if (req.configuration) {
    jb.add_raw_json ("configuration", to_json (*req.configuration));
  }
  if (req.metadata) {
    jb.add_raw_json ("metadata", *req.metadata);
  }
  jb.end_object ();
  return jb.str ();
}

std::string
to_json (const list_tasks_response &resp)
{
  json_builder jb;
  jb.begin_object ();
  jb.begin_array ();
  for (const task &t : resp.tasks) {
    jb.add_array_raw_json (to_json (t));
  }
  jb.end_array ();
  jb.add_string ("nextPageToken", resp.next_page_token);
  jb.add_int ("pageSize", resp.page_size);
  jb.add_int ("totalSize", resp.total_size);
  jb.end_object ();
  return jb.str ();
}

} // namespace agentsdk::a2a
