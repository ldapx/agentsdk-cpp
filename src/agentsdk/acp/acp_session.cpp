#include <agentsdk/common/json_builder.hpp>

#include <agentsdk/acp/acp_session.hpp>

#include <spdlog/spdlog.h>

#include <fstream>

namespace agentsdk::acp
{

acp_session::acp_session (acp_client &client) : m_client (client) {}

bool
acp_session::initialize (const client_capabilities &client_caps,
                         const implementation_info &info)
{
  json_builder jb;
  jb.begin_object ();
  jb.add_int ("protocolVersion", ACP_PROTOCOL_VERSION);

  // Client capabilities
  jb.begin_object ("clientCapabilities");
  {
    jb.begin_object ("fs");
    jb.add_bool ("readTextFile", client_caps.fs.read_text_file);
    jb.add_bool ("writeTextFile", client_caps.fs.write_text_file);
    jb.end_object ();
    jb.add_bool ("terminal", client_caps.terminal);
  }
  jb.end_object ();

  // Client info
  jb.begin_object ("clientInfo");
  jb.add_string ("name", info.name);
  jb.add_string ("title", info.title);
  jb.add_string ("version", info.version);
  jb.end_object ();

  jb.end_object ();

  // Set up notification and request handlers before initialize response
  m_client.set_notification_handler (
      [this] (const std::string &method, const std::string &params) {
        handle_notification (method, params);
      });

  m_client.set_agent_request_handler (
      [this] (const std::string &method, const std::string &params) {
        return handle_agent_request (method, params);
      });

  std::string result = m_client.send_request ("initialize", jb.str ());

  if (result.empty ()) {
    spdlog::error ("[acp] Initialize failed: no response");
    return false;
  }

  // Parse response
  simdjson::dom::parser parser;
  auto doc = parser.parse (result);

  auto protocol_version = doc["protocolVersion"];
  if (protocol_version.error ()) {
    spdlog::error ("[acp] Initialize response missing protocolVersion");
    return false;
  }

  int64_t version = 0;
  if (protocol_version.get_int64 ().get (version) != 0) {
    spdlog::error ("[acp] Cannot parse protocolVersion");
    return false;
  }

  if (version != ACP_PROTOCOL_VERSION) {
    spdlog::warn ("[acp] Agent uses protocol version {}, expected {}", version,
                  ACP_PROTOCOL_VERSION);
    return false;
  }

  // Parse agent capabilities
  auto agent_caps = doc["agentCapabilities"];
  if (!agent_caps.error ()) {
    auto load_session = agent_caps["loadSession"];
    if (!load_session.error ()) {
      auto val = load_session.value ().get_bool ();
      if (!val.error ())
        m_agent_caps.load_session = val.value ();
    }

    auto prompt_caps = agent_caps["promptCapabilities"];
    if (!prompt_caps.error ()) {
      auto img = prompt_caps["image"];
      if (!img.error ()) {
        auto val = img.value ().get_bool ();
        if (!val.error ())
          m_agent_caps.prompt_caps.image = val.value ();
      }
      auto aud = prompt_caps["audio"];
      if (!aud.error ()) {
        auto val = aud.value ().get_bool ();
        if (!val.error ())
          m_agent_caps.prompt_caps.audio = val.value ();
      }
      auto emb = prompt_caps["embeddedContext"];
      if (!emb.error ()) {
        auto val = emb.value ().get_bool ();
        if (!val.error ())
          m_agent_caps.prompt_caps.embedded_context = val.value ();
      }
    }
  }

  auto agent_info = doc["agentInfo"];
  if (!agent_info.error ()) {
    auto name = agent_info["name"];
    if (!name.error ()) {
      spdlog::info ("[acp] Agent: {}",
                    std::string (name.get_string ().value ()));
    }
  }

  m_initialized = true;
  spdlog::info ("[acp] Initialized (protocol v{})", version);
  return true;
}

std::string
acp_session::new_session (const std::string &cwd)
{
  if (!m_initialized) {
    spdlog::error ("[acp] Cannot create session: not initialized");
    return "";
  }

  json_builder jb;
  jb.begin_object ();
  jb.add_string ("cwd", cwd);
  jb.begin_array ("mcpServers");
  jb.end_array ();
  jb.end_object ();

  std::string result = m_client.send_request ("session/new", jb.str ());

  if (result.empty ()) {
    spdlog::error ("[acp] session/new failed: empty response");
    return "";
  }

  simdjson::dom::parser parser;
  auto doc = parser.parse (result);

  // Check for error response
  auto error_el = doc["error"];
  if (!error_el.error ()) {
    std::string error_json = simdjson::to_string (error_el.value ());
    spdlog::error ("[acp] session/new error: {}", error_json);
    return "";
  }

  auto session_id = doc["sessionId"];

  if (session_id.error ()) {
    spdlog::error ("[acp] session/new response missing sessionId");
    return "";
  }

  m_session_id = std::string (session_id.get_string ().value ());
  spdlog::info ("[acp] Session created: {}", m_session_id);

  // Parse configOptions
  auto config_opts = doc["configOptions"];
  if (!config_opts.error () && config_opts.value ().is_array ()) {
    for (auto opt_el : config_opts.value ().get_array ()) {
      config_option opt;

      auto id_el = opt_el["id"];
      if (!id_el.error ()) {
        std::string_view id;
        if (id_el.get_string ().get (id) == 0)
          opt.id = std::string (id);
      }

      auto name_el = opt_el["name"];
      if (!name_el.error ()) {
        std::string_view name;
        if (name_el.get_string ().get (name) == 0)
          opt.name = std::string (name);
      }

      auto desc_el = opt_el["description"];
      if (!desc_el.error ()) {
        std::string_view desc;
        if (desc_el.get_string ().get (desc) == 0)
          opt.description = std::string (desc);
      }

      auto cat_el = opt_el["category"];
      if (!cat_el.error ()) {
        std::string_view cat;
        if (cat_el.get_string ().get (cat) == 0)
          opt.category = parse_config_option_category (std::string (cat));
      }

      auto type_el = opt_el["type"];
      if (!type_el.error ()) {
        std::string_view type;
        if (type_el.get_string ().get (type) == 0)
          opt.type = parse_config_option_type (std::string (type));
      }

      auto val_el = opt_el["currentValue"];
      if (!val_el.error ()) {
        std::string_view val;
        if (val_el.get_string ().get (val) == 0)
          opt.current_value = std::string (val);
      }

      auto opts_el = opt_el["options"];
      if (!opts_el.error () && opts_el.value ().is_array ()) {
        for (auto v : opts_el.value ().get_array ()) {
          config_option_value cov;

          auto v_val = v["value"];
          if (!v_val.error ()) {
            std::string_view val;
            if (v_val.get_string ().get (val) == 0)
              cov.value = std::string (val);
          }

          auto v_name = v["name"];
          if (!v_name.error ()) {
            std::string_view name;
            if (v_name.get_string ().get (name) == 0)
              cov.name = std::string (name);
          }

          auto v_desc = v["description"];
          if (!v_desc.error ()) {
            std::string_view desc;
            if (v_desc.get_string ().get (desc) == 0)
              cov.description = std::string (desc);
          }

          opt.options.push_back (std::move (cov));
        }
      }

      spdlog::info ("[acp] Config option: {} = {}", opt.id, opt.current_value);
      m_config_options.push_back (std::move (opt));
    }
  }

  return m_session_id;
}

bool
acp_session::prompt (const std::string &text)
{
  std::vector<content_block> content;
  content.push_back (text_content{ text });
  return prompt_with_content (content);
}

bool
acp_session::prompt_with_content (const std::vector<content_block> &content)
{
  if (m_session_id.empty ()) {
    spdlog::error ("[acp] Cannot prompt: no active session");
    return false;
  }

  if (m_processing) {
    spdlog::warn ("[acp] Prompt already in progress");
    return false;
  }

  json_builder jb;
  jb.begin_object ();
  jb.add_string ("sessionId", m_session_id);

  // Build prompt content array
  jb.begin_array ("prompt");
  for (auto &block : content) {
    if (auto *text = std::get_if<text_content> (&block)) {
      jb.begin_object ();
      jb.add_string ("type", "text");
      jb.add_string ("text", text->text);
      jb.end_object ();
    }
    // Other content types can be added as needed
  }
  jb.end_array ();

  jb.end_object ();

  m_processing = true;
  // Agentic turns routinely run for minutes; wait indefinitely.  The
  // wait is still woken by the agent's response or by terminate().
  std::string result = m_client.send_request ("session/prompt", jb.str (),
                                              std::chrono::milliseconds{ 0 });
  m_processing = false;

  if (result.empty ()) {
    spdlog::error ("[acp] session/prompt failed");
    return false;
  }

  return true;
}

void
acp_session::cancel ()
{
  if (m_session_id.empty ())
    return;

  json_builder jb;
  jb.begin_object ();
  jb.add_string ("sessionId", m_session_id);
  jb.end_object ();

  m_client.send_notification ("session/cancel", jb.str ());
  m_processing = false;

  spdlog::info ("[acp] Prompt cancelled");
}

void
acp_session::close_session ()
{
  if (m_session_id.empty ())
    return;

  json_builder jb;
  jb.begin_object ();
  jb.add_string ("sessionId", m_session_id);
  jb.end_object ();

  m_client.send_request ("session/close", jb.str ());
  spdlog::info ("[acp] Session closed: {}", m_session_id);

  m_session_id.clear ();
}

std::vector<config_option>
acp_session::set_config_option (const std::string &config_id,
                                const std::string &value)
{
  if (m_session_id.empty ()) {
    spdlog::error ("[acp] Cannot set config: no active session");
    return {};
  }

  json_builder jb;
  jb.begin_object ();
  jb.add_string ("sessionId", m_session_id);
  jb.add_string ("configId", config_id);
  jb.add_string ("value", value);
  jb.end_object ();

  std::string result
      = m_client.send_request ("session/set_config_option", jb.str ());

  if (result.empty ()) {
    spdlog::error ("[acp] set_config_option failed");
    return {};
  }

  // Parse the updated config options from the response
  simdjson::dom::parser parser;
  auto doc = parser.parse (result);

  auto config_opts = doc["configOptions"];
  if (config_opts.error () || !config_opts.value ().is_array ()) {
    spdlog::error ("[acp] set_config_option response missing configOptions");
    return {};
  }

  m_config_options.clear ();
  for (auto opt_el : config_opts.value ().get_array ()) {
    config_option opt;

    auto id_el = opt_el["id"];
    if (!id_el.error ()) {
      std::string_view id;
      if (id_el.get_string ().get (id) == 0)
        opt.id = std::string (id);
    }

    auto name_el = opt_el["name"];
    if (!name_el.error ()) {
      std::string_view name;
      if (name_el.get_string ().get (name) == 0)
        opt.name = std::string (name);
    }

    auto desc_el = opt_el["description"];
    if (!desc_el.error ()) {
      std::string_view desc;
      if (desc_el.get_string ().get (desc) == 0)
        opt.description = std::string (desc);
    }

    auto cat_el = opt_el["category"];
    if (!cat_el.error ()) {
      std::string_view cat;
      if (cat_el.get_string ().get (cat) == 0)
        opt.category = parse_config_option_category (std::string (cat));
    }

    auto type_el = opt_el["type"];
    if (!type_el.error ()) {
      std::string_view type;
      if (type_el.get_string ().get (type) == 0)
        opt.type = parse_config_option_type (std::string (type));
    }

    auto val_el = opt_el["currentValue"];
    if (!val_el.error ()) {
      std::string_view val;
      if (val_el.get_string ().get (val) == 0)
        opt.current_value = std::string (val);
    }

    auto opts_el = opt_el["options"];
    if (!opts_el.error () && opts_el.value ().is_array ()) {
      for (auto v : opts_el.value ().get_array ()) {
        config_option_value cov;

        auto v_val = v["value"];
        if (!v_val.error ()) {
          std::string_view val;
          if (v_val.get_string ().get (val) == 0)
            cov.value = std::string (val);
        }

        auto v_name = v["name"];
        if (!v_name.error ()) {
          std::string_view name;
          if (v_name.get_string ().get (name) == 0)
            cov.name = std::string (name);
        }

        auto v_desc = v["description"];
        if (!v_desc.error ()) {
          std::string_view desc;
          if (v_desc.get_string ().get (desc) == 0)
            cov.description = std::string (desc);
        }

        opt.options.push_back (std::move (cov));
      }
    }

    m_config_options.push_back (std::move (opt));
  }

  spdlog::info ("[acp] Config updated ({} options)", m_config_options.size ());
  return m_config_options;
}

void
acp_session::set_update_handler (update_handler handler)
{
  m_on_update = std::move (handler);
}

void
acp_session::set_agent_request_handler (agent_request_handler handler)
{
  m_on_agent_request = std::move (handler);
}

const std::string &
acp_session::session_id () const
{
  return m_session_id;
}

const agent_capabilities &
acp_session::agent_caps () const
{
  return m_agent_caps;
}

const std::vector<config_option> &
acp_session::config_options () const
{
  return m_config_options;
}

bool
acp_session::is_initialized () const
{
  return m_initialized;
}

bool
acp_session::is_processing () const
{
  return m_processing;
}

void
acp_session::handle_notification (const std::string &method,
                                  const std::string &params)
{
  if (method == "session/update") {
    if (m_on_update) {
      m_on_update (method, params);
    }
  } else {
  }
}

agent_response
acp_session::handle_agent_request (const std::string &method,
                                   const std::string &params)
{
  // Give the application layer first crack at custom UX (interactive
  // permission dialogs etc.); fall back to protocol defaults.
  if (m_on_agent_request) {
    agent_response response = m_on_agent_request (method, params);
    if (response.handled) {
      return response;
    }
  }

  if (method == "session/request_permission") {
    return handle_permission_request (params);
  }

  if (method == "fs/read_text_file") {
    return handle_fs_read_text_file (params);
  }

  if (method == "fs/write_text_file") {
    return handle_fs_write_text_file (params);
  }

  spdlog::warn ("[acp] Agent requested unsupported method: {}", method);
  return {};
}

agent_response
acp_session::handle_permission_request (const std::string &params)
{
  agent_response response;
  response.handled = true;

  simdjson::dom::parser parser;
  auto doc = parser.parse (params);

  std::vector<permission_option> options;
  auto options_el = doc["options"];
  if (!options_el.error () && options_el.value ().is_array ()) {
    for (auto opt_el : options_el.value ().get_array ()) {
      permission_option opt;

      auto id_el = opt_el["optionId"];
      if (!id_el.error ()) {
        std::string_view id;
        if (id_el.get_string ().get (id) == 0)
          opt.option_id = std::string (id);
      }

      auto name_el = opt_el["name"];
      if (!name_el.error ()) {
        std::string_view name;
        if (name_el.get_string ().get (name) == 0)
          opt.name = std::string (name);
      }

      auto kind_el = opt_el["kind"];
      if (!kind_el.error ()) {
        std::string_view kind;
        if (kind_el.get_string ().get (kind) == 0) {
          if (kind == "allow_once")
            opt.kind = permission_option_kind::allow_once;
          else if (kind == "allow_always")
            opt.kind = permission_option_kind::allow_always;
          else if (kind == "reject_once")
            opt.kind = permission_option_kind::reject_once;
          else if (kind == "reject_always")
            opt.kind = permission_option_kind::reject_always;
        }
      }

      options.push_back (std::move (opt));
    }
  }

  // No UI for interactive permission decisions yet, so pick the least
  // restrictive option: prefer allow_once, then allow_always, then
  // whatever the agent listed first.
  const permission_option *selected = nullptr;
  for (const auto &opt : options) {
    if (opt.kind == permission_option_kind::allow_once) {
      selected = &opt;
      break;
    }
  }
  if (!selected) {
    for (const auto &opt : options) {
      if (opt.kind == permission_option_kind::allow_always) {
        selected = &opt;
        break;
      }
    }
  }
  if (!selected && !options.empty ()) {
    selected = &options.front ();
  }

  if (!selected) {
    spdlog::warn ("[acp] Permission request has no options; cancelling turn");
    response.result_json = R"({"outcome":{"outcome":"cancelled"}})";
    return response;
  }

  spdlog::info ("[acp] Auto-approving tool call ({})", selected->name);

  json_builder jb;
  jb.begin_object ();
  jb.begin_object ("outcome");
  jb.add_string ("outcome", "selected");
  jb.add_string ("optionId", selected->option_id);
  jb.end_object ();
  jb.end_object ();

  response.result_json = jb.str ();
  return response;
}

agent_response
acp_session::handle_fs_read_text_file (const std::string &params)
{
  agent_response response;
  response.handled = true;

  simdjson::dom::parser parser;
  auto doc = parser.parse (params);

  std::string path;
  auto path_el = doc["path"];
  if (!path_el.error ()) {
    std::string_view p;
    if (path_el.get_string ().get (p) == 0)
      path = std::string (p);
  }

  uint64_t line = 0;
  bool has_line = false;
  auto line_el = doc["line"];
  if (!line_el.error () && line_el.value ().is_uint64 ()) {
    line = line_el.value ().get_uint64 ().value_unsafe ();
    has_line = true;
  }

  uint64_t limit = 0;
  bool has_limit = false;
  auto limit_el = doc["limit"];
  if (!limit_el.error () && limit_el.value ().is_uint64 ()) {
    limit = limit_el.value ().get_uint64 ().value_unsafe ();
    has_limit = true;
  }

  if (path.empty ()) {
    response.ok = false;
    response.error_message = "fs/read_text_file: missing path";
    return response;
  }

  std::ifstream file (path);
  if (!file) {
    response.ok = false;
    response.error_code = -32000;
    response.error_message = "fs/read_text_file: cannot open '" + path + "'";
    return response;
  }

  std::string content;
  std::string file_line;
  uint64_t current_line = 0;
  uint64_t emitted = 0;

  while (std::getline (file, file_line)) {
    if (has_line && current_line < line) {
      ++current_line;
      continue;
    }

    content += file_line;
    content += '\n';
    ++current_line;
    ++emitted;

    if (has_limit && emitted >= limit) {
      break;
    }
  }

  json_builder jb;
  jb.begin_object ();
  jb.add_string ("content", content);
  jb.end_object ();

  response.result_json = jb.str ();
  return response;
}

agent_response
acp_session::handle_fs_write_text_file (const std::string &params)
{
  agent_response response;
  response.handled = true;

  simdjson::dom::parser parser;
  auto doc = parser.parse (params);

  std::string path;
  auto path_el = doc["path"];
  if (!path_el.error ()) {
    std::string_view p;
    if (path_el.get_string ().get (p) == 0)
      path = std::string (p);
  }

  std::string content;
  auto content_el = doc["content"];
  if (!content_el.error ()) {
    std::string_view c;
    if (content_el.get_string ().get (c) == 0)
      content = std::string (c);
  }

  if (path.empty ()) {
    response.ok = false;
    response.error_code = -32000;
    response.error_message = "fs/write_text_file: missing path";
    return response;
  }

  std::ofstream file (path, std::ios::binary | std::ios::trunc);
  if (!file) {
    response.ok = false;
    response.error_code = -32000;
    response.error_message = "fs/write_text_file: cannot open '" + path + "'";
    return response;
  }

  file << content;
  if (!file.good ()) {
    response.ok = false;
    response.error_code = -32000;
    response.error_message
        = "fs/write_text_file: write failed for '" + path + "'";
    return response;
  }

  response.result_json = "{}";
  return response;
}

} // namespace agentsdk::acp
