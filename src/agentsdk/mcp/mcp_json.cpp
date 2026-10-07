#include <agentsdk/mcp/mcp_json.hpp>

#include <agentsdk/common/json_builder.hpp>

#include <spdlog/fmt/fmt.h>

namespace agentsdk::mcp
{

namespace
{

mcp_error
param_error (const std::string &message)
{
  return mcp_error{ INVALID_PARAMS, message, std::nullopt };
}

result<std::string, mcp_error>
get_string (simdjson::dom::element element, const std::string &field)
{
  auto r = element[field].get_string ();
  if (r.error ()) {
    return param_error ("Missing or invalid field: " + field);
  }
  return std::string (r.value ());
}

std::optional<std::string>
get_optional_string (simdjson::dom::element element, const std::string &field)
{
  auto r = element[field].get_string ();
  if (r.error ()) {
    return std::nullopt;
  }
  return std::string (r.value ());
}

bool
get_bool_default (simdjson::dom::element element, const std::string &field,
                  bool dflt)
{
  auto r = element[field].get_bool ();
  if (r.error ()) {
    return dflt;
  }
  return r.value ();
}

std::optional<int64_t>
get_optional_int64 (simdjson::dom::element element, const std::string &field)
{
  auto sub = element[field];
  if (sub.error ()) {
    return std::nullopt;
  }
  auto r = sub.value ().get_int64 ();
  if (r.error ()) {
    return std::nullopt;
  }
  return r.value ();
}

std::optional<double>
get_optional_double (simdjson::dom::element element, const std::string &field)
{
  auto sub = element[field];
  if (sub.error ()) {
    return std::nullopt;
  }
  auto d = sub.value ().get_double ();
  if (!d.error ()) {
    return d.value ();
  }
  auto i = sub.value ().get_int64 ();
  if (!i.error ()) {
    return static_cast<double> (i.value ());
  }
  return std::nullopt;
}

std::optional<std::string>
get_raw_json (simdjson::dom::element element, const std::string &field)
{
  auto sub = element[field];
  if (sub.error ()) {
    return std::nullopt;
  }
  return simdjson::to_string (sub.value ());
}

bool
has_field (simdjson::dom::element element, const std::string &field)
{
  return !element[field].error ();
}

std::vector<std::string>
get_string_array (simdjson::dom::element element, const std::string &field)
{
  std::vector<std::string> out;
  auto sub = element[field];
  if (sub.error () || !sub.value ().is_array ()) {
    return out;
  }
  for (auto item : sub.value ().get_array ()) {
    auto s = item.get_string ();
    if (!s.error ()) {
      out.emplace_back (s.value ());
    }
  }
  return out;
}

} // namespace

result<simdjson::dom::element, mcp_error>
parse_json (const std::string &json)
{
  thread_local simdjson::dom::parser parser;
  auto r = parser.parse (json);
  if (r.error ()) {
    return mcp_error{ PARSE_ERROR,
                      fmt::format ("JSON parse error: {}",
                                   simdjson::error_message (r.error ())),
                      std::nullopt };
  }
  return r.value ();
}

// ── Capabilities / initialize ────────────────────────────────────

result<implementation_info, mcp_error>
implementation_info_from_json (simdjson::dom::element element)
{
  implementation_info info;
  auto name = get_string (element, "name");
  if (!name) {
    return name.error ();
  }
  info.name = name.value ();
  auto version = get_string (element, "version");
  if (!version) {
    return version.error ();
  }
  info.version = version.value ();
  info.title = get_optional_string (element, "title");
  return info;
}

result<client_capabilities, mcp_error>
client_capabilities_from_json (simdjson::dom::element element)
{
  client_capabilities caps;
  auto roots = element["roots"];
  if (!roots.error ()) {
    roots_capability r;
    r.list_changed = get_bool_default (roots.value (), "listChanged", false);
    caps.roots = r;
  }
  caps.sampling = has_field (element, "sampling");
  caps.elicitation = has_field (element, "elicitation");
  return caps;
}

result<server_capabilities, mcp_error>
server_capabilities_from_json (simdjson::dom::element element)
{
  server_capabilities caps;
  auto tools = element["tools"];
  if (!tools.error ()) {
    tools_capability t;
    t.list_changed = get_bool_default (tools.value (), "listChanged", false);
    caps.tools = t;
  }
  auto resources = element["resources"];
  if (!resources.error ()) {
    resources_capability r;
    r.subscribe = get_bool_default (resources.value (), "subscribe", false);
    r.list_changed
        = get_bool_default (resources.value (), "listChanged", false);
    caps.resources = r;
  }
  auto prompts = element["prompts"];
  if (!prompts.error ()) {
    prompts_capability p;
    p.list_changed = get_bool_default (prompts.value (), "listChanged", false);
    caps.prompts = p;
  }
  caps.logging = has_field (element, "logging");
  caps.completions = has_field (element, "completions");
  return caps;
}

result<initialize_result, mcp_error>
initialize_result_from_json (simdjson::dom::element element)
{
  initialize_result r;
  auto version = get_string (element, "protocolVersion");
  if (!version) {
    return version.error ();
  }
  r.protocol_version = version.value ();

  auto caps_el = element["capabilities"];
  if (caps_el.error ()) {
    return param_error ("Missing capabilities field");
  }
  auto caps = server_capabilities_from_json (caps_el.value ());
  if (!caps) {
    return caps.error ();
  }
  r.capabilities = caps.value ();

  auto info_el = element["serverInfo"];
  if (info_el.error ()) {
    return param_error ("Missing serverInfo field");
  }
  auto info = implementation_info_from_json (info_el.value ());
  if (!info) {
    return info.error ();
  }
  r.server_info = info.value ();
  r.instructions = get_optional_string (element, "instructions");
  return r;
}

// ── Annotations / content ────────────────────────────────────────

result<annotations, mcp_error>
annotations_from_json (simdjson::dom::element element)
{
  annotations a;
  a.audience = get_string_array (element, "audience");
  a.priority = get_optional_double (element, "priority");
  a.last_modified = get_optional_string (element, "lastModified");
  return a;
}

namespace
{

result<annotations, mcp_error>
optional_annotations (simdjson::dom::element element)
{
  auto sub = element["annotations"];
  if (sub.error ()) {
    return annotations{};
  }
  return annotations_from_json (sub.value ());
}

} // namespace

result<content_block, mcp_error>
content_block_from_json (simdjson::dom::element element)
{
  auto type_r = get_string (element, "type");
  if (!type_r) {
    return type_r.error ();
  }
  const std::string &type = type_r.value ();

  if (type == "text") {
    text_content t;
    auto s = get_string (element, "text");
    if (!s) {
      return s.error ();
    }
    t.text = s.value ();
    auto a = optional_annotations (element);
    if (a
        && (a.value ().priority || !a.value ().audience.empty ()
            || a.value ().last_modified)) {
      t.annot = a.value ();
    }
    return content_block{ t };
  }
  if (type == "image") {
    image_content c;
    auto d = get_string (element, "data");
    if (!d) {
      return d.error ();
    }
    c.data = d.value ();
    auto m = get_string (element, "mimeType");
    if (!m) {
      return m.error ();
    }
    c.mime_type = m.value ();
    auto a = optional_annotations (element);
    if (a
        && (a.value ().priority || !a.value ().audience.empty ()
            || a.value ().last_modified)) {
      c.annot = a.value ();
    }
    return content_block{ c };
  }
  if (type == "audio") {
    audio_content c;
    auto d = get_string (element, "data");
    if (!d) {
      return d.error ();
    }
    c.data = d.value ();
    auto m = get_string (element, "mimeType");
    if (!m) {
      return m.error ();
    }
    c.mime_type = m.value ();
    auto a = optional_annotations (element);
    if (a
        && (a.value ().priority || !a.value ().audience.empty ()
            || a.value ().last_modified)) {
      c.annot = a.value ();
    }
    return content_block{ c };
  }
  if (type == "resource_link") {
    resource_link l;
    auto u = get_string (element, "uri");
    if (!u) {
      return u.error ();
    }
    l.uri = u.value ();
    auto n = get_string (element, "name");
    if (!n) {
      return n.error ();
    }
    l.name = n.value ();
    l.title = get_optional_string (element, "title");
    l.description = get_optional_string (element, "description");
    l.mime_type = get_optional_string (element, "mimeType");
    auto a = optional_annotations (element);
    if (a
        && (a.value ().priority || !a.value ().audience.empty ()
            || a.value ().last_modified)) {
      l.annot = a.value ();
    }
    return content_block{ l };
  }
  if (type == "resource") {
    auto res_el = element["resource"];
    if (res_el.error ()) {
      return param_error ("Missing resource field");
    }
    simdjson::dom::element res = res_el.value ();
    embedded_resource e;
    auto u = get_string (res, "uri");
    if (!u) {
      return u.error ();
    }
    e.uri = u.value ();
    e.mime_type = get_optional_string (res, "mimeType");
    e.text = get_optional_string (res, "text");
    e.blob = get_optional_string (res, "blob");
    if (!e.text && !e.blob) {
      return param_error ("Embedded resource needs text or blob");
    }
    auto a = optional_annotations (res);
    if (a
        && (a.value ().priority || !a.value ().audience.empty ()
            || a.value ().last_modified)) {
      e.annot = a.value ();
    }
    return content_block{ e };
  }
  return param_error ("Unknown content type: " + type);
}

// ── Tools ────────────────────────────────────────────────────────

result<tool, mcp_error>
tool_from_json (simdjson::dom::element element)
{
  tool t;
  auto name = get_string (element, "name");
  if (!name) {
    return name.error ();
  }
  t.name = name.value ();
  t.title = get_optional_string (element, "title");
  t.description = get_optional_string (element, "description");
  auto schema = get_raw_json (element, "inputSchema");
  t.input_schema = schema.value_or ("{}");
  t.output_schema = get_raw_json (element, "outputSchema");
  t.annotations_json = get_raw_json (element, "annotations");
  return t;
}

result<tools_list_result, mcp_error>
tools_list_from_json (simdjson::dom::element element)
{
  tools_list_result r;
  auto tools_el = element["tools"];
  if (!tools_el.error () && tools_el.value ().is_array ()) {
    for (auto item : tools_el.value ().get_array ()) {
      auto t = tool_from_json (item);
      if (t) {
        r.tools.push_back (t.value ());
      }
    }
  }
  r.next_cursor = get_optional_string (element, "nextCursor");
  return r;
}

result<tool_result, mcp_error>
tool_result_from_json (simdjson::dom::element element)
{
  tool_result r;
  auto content_el = element["content"];
  if (!content_el.error () && content_el.value ().is_array ()) {
    for (auto item : content_el.value ().get_array ()) {
      auto c = content_block_from_json (item);
      if (c) {
        r.content.push_back (c.value ());
      }
    }
  }
  r.structured_content = get_raw_json (element, "structuredContent");
  r.is_error = get_bool_default (element, "isError", false);
  return r;
}

// ── Resources ────────────────────────────────────────────────────

result<resource, mcp_error>
resource_from_json (simdjson::dom::element element)
{
  resource r;
  auto uri = get_string (element, "uri");
  if (!uri) {
    return uri.error ();
  }
  r.uri = uri.value ();
  auto name = get_string (element, "name");
  if (!name) {
    return name.error ();
  }
  r.name = name.value ();
  r.title = get_optional_string (element, "title");
  r.description = get_optional_string (element, "description");
  r.mime_type = get_optional_string (element, "mimeType");
  r.size = get_optional_int64 (element, "size");
  auto a = element["annotations"];
  if (!a.error ()) {
    auto parsed = annotations_from_json (a.value ());
    if (parsed) {
      r.annot = parsed.value ();
    }
  }
  return r;
}

result<resource_template, mcp_error>
resource_template_from_json (simdjson::dom::element element)
{
  resource_template t;
  auto uri = get_string (element, "uriTemplate");
  if (!uri) {
    return uri.error ();
  }
  t.uri_template = uri.value ();
  auto name = get_string (element, "name");
  if (!name) {
    return name.error ();
  }
  t.name = name.value ();
  t.title = get_optional_string (element, "title");
  t.description = get_optional_string (element, "description");
  t.mime_type = get_optional_string (element, "mimeType");
  auto a = element["annotations"];
  if (!a.error ()) {
    auto parsed = annotations_from_json (a.value ());
    if (parsed) {
      t.annot = parsed.value ();
    }
  }
  return t;
}

result<resources_list_result, mcp_error>
resources_list_from_json (simdjson::dom::element element)
{
  resources_list_result r;
  auto list_el = element["resources"];
  if (!list_el.error () && list_el.value ().is_array ()) {
    for (auto item : list_el.value ().get_array ()) {
      auto res = resource_from_json (item);
      if (res) {
        r.resources.push_back (res.value ());
      }
    }
  }
  r.next_cursor = get_optional_string (element, "nextCursor");
  return r;
}

result<resource_templates_result, mcp_error>
resource_templates_from_json (simdjson::dom::element element)
{
  resource_templates_result r;
  auto list_el = element["resourceTemplates"];
  if (!list_el.error () && list_el.value ().is_array ()) {
    for (auto item : list_el.value ().get_array ()) {
      auto t = resource_template_from_json (item);
      if (t) {
        r.templates.push_back (t.value ());
      }
    }
  }
  r.next_cursor = get_optional_string (element, "nextCursor");
  return r;
}

result<resource_contents, mcp_error>
resource_contents_from_json (simdjson::dom::element element)
{
  resource_contents c;
  auto uri = get_string (element, "uri");
  if (!uri) {
    return uri.error ();
  }
  c.uri = uri.value ();
  c.mime_type = get_optional_string (element, "mimeType");
  c.text = get_optional_string (element, "text");
  c.blob = get_optional_string (element, "blob");
  return c;
}

result<resource_read_result, mcp_error>
resource_read_from_json (simdjson::dom::element element)
{
  resource_read_result r;
  auto list_el = element["contents"];
  if (!list_el.error () && list_el.value ().is_array ()) {
    for (auto item : list_el.value ().get_array ()) {
      auto c = resource_contents_from_json (item);
      if (c) {
        r.contents.push_back (c.value ());
      }
    }
  }
  return r;
}

// ── Prompts ──────────────────────────────────────────────────────

result<prompt, mcp_error>
prompt_from_json (simdjson::dom::element element)
{
  prompt p;
  auto name = get_string (element, "name");
  if (!name) {
    return name.error ();
  }
  p.name = name.value ();
  p.title = get_optional_string (element, "title");
  p.description = get_optional_string (element, "description");
  auto args_el = element["arguments"];
  if (!args_el.error () && args_el.value ().is_array ()) {
    for (auto item : args_el.value ().get_array ()) {
      prompt_argument a;
      auto an = get_string (item, "name");
      if (!an) {
        continue;
      }
      a.name = an.value ();
      a.title = get_optional_string (item, "title");
      a.description = get_optional_string (item, "description");
      a.required = get_bool_default (item, "required", false);
      p.arguments.push_back (std::move (a));
    }
  }
  return p;
}

result<prompts_list_result, mcp_error>
prompts_list_from_json (simdjson::dom::element element)
{
  prompts_list_result r;
  auto list_el = element["prompts"];
  if (!list_el.error () && list_el.value ().is_array ()) {
    for (auto item : list_el.value ().get_array ()) {
      auto p = prompt_from_json (item);
      if (p) {
        r.prompts.push_back (p.value ());
      }
    }
  }
  r.next_cursor = get_optional_string (element, "nextCursor");
  return r;
}

result<prompt_message, mcp_error>
prompt_message_from_json (simdjson::dom::element element)
{
  prompt_message m;
  auto role = get_string (element, "role");
  if (!role) {
    return role.error ();
  }
  m.role = role.value ();
  auto content_el = element["content"];
  if (content_el.error ()) {
    return param_error ("Missing content field");
  }
  auto content = content_block_from_json (content_el.value ());
  if (!content) {
    return content.error ();
  }
  m.content = content.value ();
  return m;
}

result<prompt_result, mcp_error>
prompt_result_from_json (simdjson::dom::element element)
{
  prompt_result r;
  r.description = get_optional_string (element, "description");
  auto msgs_el = element["messages"];
  if (!msgs_el.error () && msgs_el.value ().is_array ()) {
    for (auto item : msgs_el.value ().get_array ()) {
      auto m = prompt_message_from_json (item);
      if (m) {
        r.messages.push_back (m.value ());
      }
    }
  }
  return r;
}

// ── Sampling / roots / elicitation ───────────────────────────────

result<sampling_message, mcp_error>
sampling_message_from_json (simdjson::dom::element element)
{
  sampling_message m;
  auto role = get_string (element, "role");
  if (!role) {
    return role.error ();
  }
  m.role = role.value ();
  auto content_el = element["content"];
  if (content_el.error ()) {
    return param_error ("Missing content field");
  }
  auto content = content_block_from_json (content_el.value ());
  if (!content) {
    return content.error ();
  }
  m.content = content.value ();
  return m;
}

result<sampling_params, mcp_error>
sampling_params_from_json (simdjson::dom::element element)
{
  sampling_params p;
  auto msgs_el = element["messages"];
  if (msgs_el.error () || !msgs_el.value ().is_array ()) {
    return param_error ("Missing messages field");
  }
  for (auto item : msgs_el.value ().get_array ()) {
    auto m = sampling_message_from_json (item);
    if (m) {
      p.messages.push_back (m.value ());
    }
  }
  auto prefs_el = element["modelPreferences"];
  if (!prefs_el.error ()) {
    model_preferences prefs;
    simdjson::dom::element prefs_obj = prefs_el.value ();
    auto hints_el = prefs_obj["hints"];
    if (!hints_el.error () && hints_el.value ().is_array ()) {
      for (auto h : hints_el.value ().get_array ()) {
        model_hint hint;
        hint.name = get_optional_string (h, "name");
        prefs.hints.push_back (std::move (hint));
      }
    }
    prefs.cost_priority = get_optional_double (prefs_obj, "costPriority");
    prefs.speed_priority = get_optional_double (prefs_obj, "speedPriority");
    prefs.intelligence_priority
        = get_optional_double (prefs_obj, "intelligencePriority");
    p.preferences = std::move (prefs);
  }
  p.system_prompt = get_optional_string (element, "systemPrompt");
  p.include_context = get_optional_string (element, "includeContext");
  p.temperature = get_optional_double (element, "temperature");
  auto max_tokens = get_optional_int64 (element, "maxTokens");
  if (!max_tokens) {
    return param_error ("Missing maxTokens field");
  }
  p.max_tokens = *max_tokens;
  p.stop_sequences = get_string_array (element, "stopSequences");
  p.metadata = get_raw_json (element, "metadata");
  return p;
}

result<sampling_result, mcp_error>
sampling_result_from_json (simdjson::dom::element element)
{
  sampling_result r;
  auto role = get_string (element, "role");
  if (!role) {
    return role.error ();
  }
  r.role = role.value ();
  auto content_el = element["content"];
  if (content_el.error ()) {
    return param_error ("Missing content field");
  }
  auto content = content_block_from_json (content_el.value ());
  if (!content) {
    return content.error ();
  }
  r.content = content.value ();
  auto model = get_string (element, "model");
  if (!model) {
    return model.error ();
  }
  r.model = model.value ();
  r.stop_reason = get_optional_string (element, "stopReason");
  return r;
}

result<roots_list_result, mcp_error>
roots_list_from_json (simdjson::dom::element element)
{
  roots_list_result r;
  auto list_el = element["roots"];
  if (!list_el.error () && list_el.value ().is_array ()) {
    for (auto item : list_el.value ().get_array ()) {
      root entry;
      auto uri = get_string (item, "uri");
      if (!uri) {
        continue;
      }
      entry.uri = uri.value ();
      entry.name = get_optional_string (item, "name");
      r.roots.push_back (std::move (entry));
    }
  }
  return r;
}

result<elicitation_params, mcp_error>
elicitation_params_from_json (simdjson::dom::element element)
{
  elicitation_params p;
  auto message = get_string (element, "message");
  if (!message) {
    return message.error ();
  }
  p.message = message.value ();
  auto schema = get_raw_json (element, "requestedSchema");
  p.requested_schema = schema.value_or ("{}");
  return p;
}

result<elicitation_result, mcp_error>
elicitation_result_from_json (simdjson::dom::element element)
{
  elicitation_result r;
  auto action = get_string (element, "action");
  if (!action) {
    return action.error ();
  }
  r.action = action.value ();
  r.content = get_raw_json (element, "content");
  return r;
}

// ── Logging / completion / progress ─────────────────────────────

result<log_message, mcp_error>
log_message_from_json (simdjson::dom::element element)
{
  log_message m;
  auto level = get_string (element, "level");
  if (!level) {
    return level.error ();
  }
  auto parsed = parse_log_level (level.value ());
  if (!parsed) {
    return param_error ("Unknown log level: " + level.value ());
  }
  m.level = *parsed;
  m.logger = get_optional_string (element, "logger");
  auto data = get_raw_json (element, "data");
  m.data = data.value_or ("null");
  return m;
}

result<completion_params, mcp_error>
completion_params_from_json (simdjson::dom::element element)
{
  completion_params p;
  auto ref_el = element["ref"];
  if (ref_el.error ()) {
    return param_error ("Missing ref field");
  }
  simdjson::dom::element ref = ref_el.value ();
  auto type = get_string (ref, "type");
  if (!type) {
    return type.error ();
  }
  p.ref.type = type.value ();
  p.ref.name = get_optional_string (ref, "name");
  p.ref.uri = get_optional_string (ref, "uri");
  auto arg_el = element["argument"];
  if (arg_el.error ()) {
    return param_error ("Missing argument field");
  }
  simdjson::dom::element arg = arg_el.value ();
  auto name = get_string (arg, "name");
  if (!name) {
    return name.error ();
  }
  p.argument_name = name.value ();
  auto value = get_string (arg, "value");
  if (!value) {
    return value.error ();
  }
  p.argument_value = value.value ();
  auto ctx_el = element["context"];
  if (!ctx_el.error ()) {
    auto args = get_raw_json (ctx_el.value (), "arguments");
    if (args) {
      p.context_arguments = args;
    }
  }
  return p;
}

result<completion_result, mcp_error>
completion_result_from_json (simdjson::dom::element element)
{
  completion_result r;
  auto completion_el = element["completion"];
  if (completion_el.error ()) {
    return param_error ("Missing completion field");
  }
  simdjson::dom::element completion = completion_el.value ();
  r.values = get_string_array (completion, "values");
  r.total = get_optional_int64 (completion, "total");
  r.has_more = get_bool_default (completion, "hasMore", false);
  return r;
}

result<progress_notification, mcp_error>
progress_notification_from_json (simdjson::dom::element element)
{
  progress_notification n;
  auto token_el = element["progressToken"];
  if (token_el.error ()) {
    return param_error ("Missing progressToken field");
  }
  auto as_int = token_el.value ().get_int64 ();
  if (!as_int.error ()) {
    n.token = as_int.value ();
  } else {
    auto as_str = token_el.value ().get_string ();
    if (as_str.error ()) {
      return param_error ("Invalid progressToken field");
    }
    n.token = std::string (as_str.value ());
  }
  auto progress = get_optional_double (element, "progress");
  if (!progress) {
    return param_error ("Missing progress field");
  }
  n.progress = *progress;
  n.total = get_optional_double (element, "total");
  n.message = get_optional_string (element, "message");
  return n;
}

// ── Serialize ────────────────────────────────────────────────────

std::string
to_json (const implementation_info &info)
{
  json_builder jb;
  jb.begin_object ();
  jb.add_string ("name", info.name);
  jb.add_optional_string ("title", info.title);
  jb.add_string ("version", info.version);
  jb.end_object ();
  return jb.str ();
}

std::string
to_json (const client_capabilities &caps)
{
  json_builder jb;
  jb.begin_object ();
  if (caps.roots) {
    jb.begin_object ("roots");
    jb.add_bool ("listChanged", caps.roots->list_changed);
    jb.end_object ();
  }
  if (caps.sampling) {
    jb.begin_object ("sampling");
    jb.end_object ();
  }
  if (caps.elicitation) {
    jb.begin_object ("elicitation");
    jb.end_object ();
  }
  jb.end_object ();
  return jb.str ();
}

std::string
to_json (const server_capabilities &caps)
{
  json_builder jb;
  jb.begin_object ();
  if (caps.tools) {
    jb.begin_object ("tools");
    jb.add_bool ("listChanged", caps.tools->list_changed);
    jb.end_object ();
  }
  if (caps.resources) {
    jb.begin_object ("resources");
    jb.add_bool ("subscribe", caps.resources->subscribe);
    jb.add_bool ("listChanged", caps.resources->list_changed);
    jb.end_object ();
  }
  if (caps.prompts) {
    jb.begin_object ("prompts");
    jb.add_bool ("listChanged", caps.prompts->list_changed);
    jb.end_object ();
  }
  if (caps.logging) {
    jb.begin_object ("logging");
    jb.end_object ();
  }
  if (caps.completions) {
    jb.begin_object ("completions");
    jb.end_object ();
  }
  jb.end_object ();
  return jb.str ();
}

std::string
to_json (const initialize_result &r)
{
  json_builder jb;
  jb.begin_object ();
  jb.add_string ("protocolVersion", r.protocol_version);
  jb.add_raw_json ("capabilities", to_json (r.capabilities));
  jb.add_raw_json ("serverInfo", to_json (r.server_info));
  jb.add_optional_string ("instructions", r.instructions);
  jb.end_object ();
  return jb.str ();
}

std::string
to_json (const annotations &a)
{
  json_builder jb;
  jb.begin_object ();
  if (!a.audience.empty ()) {
    jb.begin_array ("audience");
    for (const std::string &entry : a.audience) {
      jb.add_array_string (entry);
    }
    jb.end_array ();
  }
  if (a.priority) {
    jb.add_double ("priority", *a.priority);
  }
  jb.add_optional_string ("lastModified", a.last_modified);
  jb.end_object ();
  return jb.str ();
}

namespace
{

void
add_annotations (json_builder &jb, const std::optional<annotations> &a)
{
  if (a) {
    jb.add_raw_json ("annotations", to_json (*a));
  }
}

} // namespace

std::string
to_json (const content_block &c)
{
  json_builder jb;
  jb.begin_object ();
  if (const auto *t = std::get_if<text_content> (&c)) {
    jb.add_string ("type", "text");
    jb.add_string ("text", t->text);
    add_annotations (jb, t->annot);
  } else if (const auto *i = std::get_if<image_content> (&c)) {
    jb.add_string ("type", "image");
    jb.add_string ("data", i->data);
    jb.add_string ("mimeType", i->mime_type);
    add_annotations (jb, i->annot);
  } else if (const auto *au = std::get_if<audio_content> (&c)) {
    jb.add_string ("type", "audio");
    jb.add_string ("data", au->data);
    jb.add_string ("mimeType", au->mime_type);
    add_annotations (jb, au->annot);
  } else if (const auto *l = std::get_if<resource_link> (&c)) {
    jb.add_string ("type", "resource_link");
    jb.add_string ("uri", l->uri);
    jb.add_string ("name", l->name);
    jb.add_optional_string ("title", l->title);
    jb.add_optional_string ("description", l->description);
    jb.add_optional_string ("mimeType", l->mime_type);
    add_annotations (jb, l->annot);
  } else if (const auto *e = std::get_if<embedded_resource> (&c)) {
    jb.add_string ("type", "resource");
    jb.begin_object ("resource");
    jb.add_string ("uri", e->uri);
    jb.add_optional_string ("mimeType", e->mime_type);
    jb.add_optional_string ("text", e->text);
    jb.add_optional_string ("blob", e->blob);
    add_annotations (jb, e->annot);
    jb.end_object ();
  }
  jb.end_object ();
  return jb.str ();
}

std::string
to_json (const tool &t)
{
  json_builder jb;
  jb.begin_object ();
  jb.add_string ("name", t.name);
  jb.add_optional_string ("title", t.title);
  jb.add_optional_string ("description", t.description);
  jb.add_raw_json ("inputSchema", t.input_schema);
  if (t.output_schema) {
    jb.add_raw_json ("outputSchema", *t.output_schema);
  }
  if (t.annotations_json) {
    jb.add_raw_json ("annotations", *t.annotations_json);
  }
  jb.end_object ();
  return jb.str ();
}

std::string
to_json (const tool_result &r)
{
  json_builder jb;
  jb.begin_object ();
  jb.begin_array ("content");
  for (const content_block &c : r.content) {
    jb.add_array_raw_json (to_json (c));
  }
  jb.end_array ();
  if (r.structured_content) {
    jb.add_raw_json ("structuredContent", *r.structured_content);
  }
  jb.add_bool ("isError", r.is_error);
  jb.end_object ();
  return jb.str ();
}

std::string
to_json (const resource &r)
{
  json_builder jb;
  jb.begin_object ();
  jb.add_string ("uri", r.uri);
  jb.add_string ("name", r.name);
  jb.add_optional_string ("title", r.title);
  jb.add_optional_string ("description", r.description);
  jb.add_optional_string ("mimeType", r.mime_type);
  if (r.size) {
    jb.add_int ("size", *r.size);
  }
  if (r.annot) {
    jb.add_raw_json ("annotations", to_json (*r.annot));
  }
  jb.end_object ();
  return jb.str ();
}

std::string
to_json (const resource_template &t)
{
  json_builder jb;
  jb.begin_object ();
  jb.add_string ("uriTemplate", t.uri_template);
  jb.add_string ("name", t.name);
  jb.add_optional_string ("title", t.title);
  jb.add_optional_string ("description", t.description);
  jb.add_optional_string ("mimeType", t.mime_type);
  if (t.annot) {
    jb.add_raw_json ("annotations", to_json (*t.annot));
  }
  jb.end_object ();
  return jb.str ();
}

std::string
to_json (const resource_contents &c)
{
  json_builder jb;
  jb.begin_object ();
  jb.add_string ("uri", c.uri);
  jb.add_optional_string ("mimeType", c.mime_type);
  jb.add_optional_string ("text", c.text);
  jb.add_optional_string ("blob", c.blob);
  jb.end_object ();
  return jb.str ();
}

std::string
to_json (const prompt &p)
{
  json_builder jb;
  jb.begin_object ();
  jb.add_string ("name", p.name);
  jb.add_optional_string ("title", p.title);
  jb.add_optional_string ("description", p.description);
  if (!p.arguments.empty ()) {
    jb.begin_array ("arguments");
    for (const prompt_argument &a : p.arguments) {
      json_builder ab;
      ab.begin_object ();
      ab.add_string ("name", a.name);
      ab.add_optional_string ("title", a.title);
      ab.add_optional_string ("description", a.description);
      if (a.required) {
        ab.add_bool ("required", true);
      }
      ab.end_object ();
      jb.add_array_raw_json (ab.str ());
    }
    jb.end_array ();
  }
  jb.end_object ();
  return jb.str ();
}

std::string
to_json (const prompt_message &m)
{
  json_builder jb;
  jb.begin_object ();
  jb.add_string ("role", m.role);
  jb.add_raw_json ("content", to_json (m.content));
  jb.end_object ();
  return jb.str ();
}

std::string
to_json (const prompt_result &r)
{
  json_builder jb;
  jb.begin_object ();
  jb.add_optional_string ("description", r.description);
  jb.begin_array ("messages");
  for (const prompt_message &m : r.messages) {
    jb.add_array_raw_json (to_json (m));
  }
  jb.end_array ();
  jb.end_object ();
  return jb.str ();
}

std::string
to_json (const sampling_message &m)
{
  json_builder jb;
  jb.begin_object ();
  jb.add_string ("role", m.role);
  jb.add_raw_json ("content", to_json (m.content));
  jb.end_object ();
  return jb.str ();
}

std::string
to_json (const sampling_params &p)
{
  json_builder jb;
  jb.begin_object ();
  jb.begin_array ("messages");
  for (const sampling_message &m : p.messages) {
    jb.add_array_raw_json (to_json (m));
  }
  jb.end_array ();
  if (p.preferences) {
    jb.begin_object ("modelPreferences");
    if (!p.preferences->hints.empty ()) {
      jb.begin_array ("hints");
      for (const model_hint &h : p.preferences->hints) {
        json_builder hb;
        hb.begin_object ();
        hb.add_optional_string ("name", h.name);
        hb.end_object ();
        jb.add_array_raw_json (hb.str ());
      }
      jb.end_array ();
    }
    if (p.preferences->cost_priority) {
      jb.add_double ("costPriority", *p.preferences->cost_priority);
    }
    if (p.preferences->speed_priority) {
      jb.add_double ("speedPriority", *p.preferences->speed_priority);
    }
    if (p.preferences->intelligence_priority) {
      jb.add_double ("intelligencePriority",
                     *p.preferences->intelligence_priority);
    }
    jb.end_object ();
  }
  jb.add_optional_string ("systemPrompt", p.system_prompt);
  jb.add_optional_string ("includeContext", p.include_context);
  if (p.temperature) {
    jb.add_double ("temperature", *p.temperature);
  }
  jb.add_int ("maxTokens", p.max_tokens);
  if (!p.stop_sequences.empty ()) {
    jb.begin_array ("stopSequences");
    for (const std::string &s : p.stop_sequences) {
      jb.add_array_string (s);
    }
    jb.end_array ();
  }
  if (p.metadata) {
    jb.add_raw_json ("metadata", *p.metadata);
  }
  jb.end_object ();
  return jb.str ();
}

std::string
to_json (const sampling_result &r)
{
  json_builder jb;
  jb.begin_object ();
  jb.add_string ("role", r.role);
  jb.add_raw_json ("content", to_json (r.content));
  jb.add_string ("model", r.model);
  jb.add_optional_string ("stopReason", r.stop_reason);
  jb.end_object ();
  return jb.str ();
}

std::string
to_json (const root &r)
{
  json_builder jb;
  jb.begin_object ();
  jb.add_string ("uri", r.uri);
  jb.add_optional_string ("name", r.name);
  jb.end_object ();
  return jb.str ();
}

std::string
to_json (const elicitation_params &p)
{
  json_builder jb;
  jb.begin_object ();
  jb.add_string ("message", p.message);
  jb.add_raw_json ("requestedSchema", p.requested_schema);
  jb.end_object ();
  return jb.str ();
}

std::string
to_json (const elicitation_result &r)
{
  json_builder jb;
  jb.begin_object ();
  jb.add_string ("action", r.action);
  if (r.content) {
    jb.add_raw_json ("content", *r.content);
  }
  jb.end_object ();
  return jb.str ();
}

std::string
to_json (const log_message &m)
{
  json_builder jb;
  jb.begin_object ();
  jb.add_string ("level", log_level_name (m.level));
  jb.add_optional_string ("logger", m.logger);
  jb.add_raw_json ("data", m.data);
  jb.end_object ();
  return jb.str ();
}

std::string
to_json (const completion_params &p)
{
  json_builder jb;
  jb.begin_object ();
  jb.begin_object ("ref");
  jb.add_string ("type", p.ref.type);
  jb.add_optional_string ("name", p.ref.name);
  jb.add_optional_string ("uri", p.ref.uri);
  jb.end_object ();
  jb.begin_object ("argument");
  jb.add_string ("name", p.argument_name);
  jb.add_string ("value", p.argument_value);
  jb.end_object ();
  if (p.context_arguments) {
    jb.begin_object ("context");
    jb.add_raw_json ("arguments", *p.context_arguments);
    jb.end_object ();
  }
  jb.end_object ();
  return jb.str ();
}

std::string
to_json (const completion_result &r)
{
  json_builder jb;
  jb.begin_object ();
  jb.begin_object ("completion");
  jb.begin_array ("values");
  for (const std::string &v : r.values) {
    jb.add_array_string (v);
  }
  jb.end_array ();
  if (r.total) {
    jb.add_int ("total", *r.total);
  }
  jb.add_bool ("hasMore", r.has_more);
  jb.end_object ();
  jb.end_object ();
  return jb.str ();
}

std::string
to_json (const progress_token &t)
{
  if (const auto *s = std::get_if<std::string> (&t)) {
    json_builder jb;
    // Reuse the escaping logic via a single string value.
    return "\"" + json_builder::escape (*s) + "\"";
  }
  return std::to_string (std::get<int64_t> (t));
}

std::string
to_json (const progress_notification &n)
{
  json_builder jb;
  jb.begin_object ();
  jb.add_raw_json ("progressToken", to_json (n.token));
  // progress/total may be fractional; to_string keeps full precision.
  jb.add_raw_json ("progress", std::to_string (n.progress));
  if (n.total) {
    jb.add_raw_json ("total", std::to_string (*n.total));
  }
  jb.add_optional_string ("message", n.message);
  jb.end_object ();
  return jb.str ();
}

std::string
initialize_params_json (const std::string &protocol_version,
                        const client_capabilities &caps,
                        const implementation_info &info)
{
  json_builder jb;
  jb.begin_object ();
  jb.add_string ("protocolVersion", protocol_version);
  jb.add_raw_json ("capabilities", to_json (caps));
  jb.add_raw_json ("clientInfo", to_json (info));
  jb.end_object ();
  return jb.str ();
}

// ── JSON-RPC envelopes ───────────────────────────────────────────

const std::vector<std::string> &
supported_protocol_versions ()
{
  static const std::vector<std::string> versions{
    "2025-06-18",
    "2025-03-26",
    "2024-11-05",
  };
  return versions;
}

bool
is_supported_version (const std::string &version)
{
  for (const std::string &v : supported_protocol_versions ()) {
    if (v == version) {
      return true;
    }
  }
  return false;
}

std::string
envelope_result (const std::string &id_json, const std::string &result_json)
{
  json_builder jb;
  jb.begin_object ();
  jb.add_string ("jsonrpc", "2.0");
  jb.add_raw_json ("id", id_json);
  jb.add_raw_json ("result", result_json);
  jb.end_object ();
  return jb.str ();
}

std::string
envelope_error (const std::string &id_json, int64_t code,
                const std::string &message)
{
  json_builder jb;
  jb.begin_object ();
  jb.add_string ("jsonrpc", "2.0");
  jb.add_raw_json ("id", id_json);
  jb.begin_object ("error");
  jb.add_int ("code", code);
  jb.add_string ("message", message);
  jb.end_object ();
  jb.end_object ();
  return jb.str ();
}

mcp_error
parse_rpc_error (const std::string &error_json)
{
  auto parsed = parse_json (error_json);
  if (!parsed) {
    return mcp_error{ INTERNAL_ERROR, "Request failed", error_json };
  }
  simdjson::dom::element el = parsed.value ();
  int64_t code = INTERNAL_ERROR;
  auto code_el = el["code"];
  if (!code_el.error ()) {
    if (code_el.value ().get_int64 ().get (code) != 0) {
      code = INTERNAL_ERROR;
    }
  }
  std::string message = "Request failed";
  auto msg_el = el["message"].get_string ();
  if (!msg_el.error ()) {
    message = std::string (msg_el.value ());
  }
  std::optional<std::string> data;
  auto data_el = el["data"];
  if (!data_el.error ()) {
    data = simdjson::to_string (data_el.value ());
  }
  return mcp_error{ code, message, data };
}

} // namespace agentsdk::mcp
