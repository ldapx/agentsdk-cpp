#include <agentsdk/mcp/mcp_client.hpp>

#include <spdlog/spdlog.h>

namespace agentsdk::mcp
{

mcp_client::mcp_client () = default;

mcp_client::~mcp_client () { terminate (); }

bool
mcp_client::launch_server (const std::string &command,
                           const std::vector<std::string> &args)
{
  bool ok = launch (command, args);
  if (ok) {
    start_dispatch_thread ();
  }
  return ok;
}

result<initialize_result, mcp_error>
mcp_client::initialize (const implementation_info &info,
                        const client_capabilities &caps,
                        std::chrono::milliseconds timeout)
{
  auto r = request_detailed (
      "initialize", initialize_params_json (MCP_PROTOCOL_VERSION, caps, info),
      timeout);
  if (!r) {
    return parse_rpc_error (r.error ().error_json);
  }

  auto parsed = parse_json (r.value ());
  if (!parsed) {
    return parsed.error ();
  }
  auto init = initialize_result_from_json (parsed.value ());
  if (!init) {
    return init.error ();
  }

  if (!is_supported_version (init.value ().protocol_version)) {
    return mcp_error{ INVALID_PARAMS,
                      "Unsupported protocol version: "
                          + init.value ().protocol_version,
                      std::nullopt };
  }

  {
    std::lock_guard<std::mutex> lock (m_state_mutex);
    m_protocol_version = init.value ().protocol_version;
    m_server_caps = init.value ().capabilities;
    m_server_info = init.value ().server_info;
    m_instructions = init.value ().instructions;
    m_initialized = true;
  }

  notify ("notifications/initialized", "{}");
  spdlog::info ("[mcp] Initialized (protocol {})",
                init.value ().protocol_version);
  return init.value ();
}

bool
mcp_client::is_initialized () const
{
  std::lock_guard<std::mutex> lock (m_state_mutex);
  return m_initialized;
}

std::string
mcp_client::protocol_version () const
{
  std::lock_guard<std::mutex> lock (m_state_mutex);
  return m_protocol_version;
}

server_capabilities
mcp_client::server_caps () const
{
  std::lock_guard<std::mutex> lock (m_state_mutex);
  return m_server_caps;
}

implementation_info
mcp_client::server_info () const
{
  std::lock_guard<std::mutex> lock (m_state_mutex);
  return m_server_info;
}

std::optional<std::string>
mcp_client::server_instructions () const
{
  std::lock_guard<std::mutex> lock (m_state_mutex);
  return m_instructions;
}

bool
mcp_client::is_running () const
{
  return stdio_json_rpc::is_running ();
}

void
mcp_client::terminate ()
{
  m_running.store (false);
  stop_dispatch_thread ();
  stdio_json_rpc::terminate ();
  std::lock_guard<std::mutex> lock (m_state_mutex);
  m_initialized = false;
}

result<std::string, mcp_error>
mcp_client::call (const std::string &method, const std::string &params_json,
                  std::chrono::milliseconds timeout)
{
  auto ready = require_initialized ();
  if (!ready) {
    return ready.error ();
  }
  auto r = request_detailed (method, params_json, timeout);
  if (!r) {
    return parse_rpc_error (r.error ().error_json);
  }
  return result<std::string, mcp_error>{ r.value () };
}

result<std::string, mcp_error>
mcp_client::require_initialized () const
{
  std::lock_guard<std::mutex> lock (m_state_mutex);
  if (!m_initialized) {
    return mcp_error{ INVALID_REQUEST,
                      "Client is not initialized: call initialize() first",
                      std::nullopt };
  }
  return result<std::string, mcp_error>{ std::string{} };
}

result<void, mcp_error>
mcp_client::ping (std::chrono::milliseconds timeout)
{
  auto r = call ("ping", "{}", timeout);
  if (!r) {
    return r.error ();
  }
  return result<void, mcp_error>{};
}

result<tools_list_result, mcp_error>
mcp_client::list_tools (const std::optional<std::string> &cursor,
                        std::chrono::milliseconds timeout)
{
  json_builder jb;
  jb.begin_object ();
  jb.add_optional_string ("cursor", cursor);
  jb.end_object ();
  auto r = call ("tools/list", jb.str (), timeout);
  if (!r) {
    return r.error ();
  }
  auto parsed = parse_json (r.value ());
  if (!parsed) {
    return parsed.error ();
  }
  return tools_list_from_json (parsed.value ());
}

result<std::vector<tool>, mcp_error>
mcp_client::list_all_tools (std::chrono::milliseconds timeout)
{
  std::vector<tool> out;
  std::optional<std::string> cursor;
  while (true) {
    auto page = list_tools (cursor, timeout);
    if (!page) {
      return page.error ();
    }
    for (const tool &t : page.value ().tools) {
      out.push_back (t);
    }
    if (!page.value ().next_cursor) {
      break;
    }
    cursor = page.value ().next_cursor;
  }
  return out;
}

result<tool_result, mcp_error>
mcp_client::call_tool (const std::string &name,
                       const std::string &arguments_json,
                       std::chrono::milliseconds timeout)
{
  json_builder jb;
  jb.begin_object ();
  jb.add_string ("name", name);
  jb.add_raw_json ("arguments", arguments_json);
  jb.end_object ();
  auto r = call ("tools/call", jb.str (), timeout);
  if (!r) {
    return r.error ();
  }
  auto parsed = parse_json (r.value ());
  if (!parsed) {
    return parsed.error ();
  }
  return tool_result_from_json (parsed.value ());
}

result<resources_list_result, mcp_error>
mcp_client::list_resources (const std::optional<std::string> &cursor,
                            std::chrono::milliseconds timeout)
{
  json_builder jb;
  jb.begin_object ();
  jb.add_optional_string ("cursor", cursor);
  jb.end_object ();
  auto r = call ("resources/list", jb.str (), timeout);
  if (!r) {
    return r.error ();
  }
  auto parsed = parse_json (r.value ());
  if (!parsed) {
    return parsed.error ();
  }
  return resources_list_from_json (parsed.value ());
}

result<std::vector<resource>, mcp_error>
mcp_client::list_all_resources (std::chrono::milliseconds timeout)
{
  std::vector<resource> out;
  std::optional<std::string> cursor;
  while (true) {
    auto page = list_resources (cursor, timeout);
    if (!page) {
      return page.error ();
    }
    for (const resource &res : page.value ().resources) {
      out.push_back (res);
    }
    if (!page.value ().next_cursor) {
      break;
    }
    cursor = page.value ().next_cursor;
  }
  return out;
}

result<resource_templates_result, mcp_error>
mcp_client::list_resource_templates (const std::optional<std::string> &cursor,
                                     std::chrono::milliseconds timeout)
{
  json_builder jb;
  jb.begin_object ();
  jb.add_optional_string ("cursor", cursor);
  jb.end_object ();
  auto r = call ("resources/templates/list", jb.str (), timeout);
  if (!r) {
    return r.error ();
  }
  auto parsed = parse_json (r.value ());
  if (!parsed) {
    return parsed.error ();
  }
  return resource_templates_from_json (parsed.value ());
}

result<resource_read_result, mcp_error>
mcp_client::read_resource (const std::string &uri,
                           std::chrono::milliseconds timeout)
{
  json_builder jb;
  jb.begin_object ();
  jb.add_string ("uri", uri);
  jb.end_object ();
  auto r = call ("resources/read", jb.str (), timeout);
  if (!r) {
    return r.error ();
  }
  auto parsed = parse_json (r.value ());
  if (!parsed) {
    return parsed.error ();
  }
  return resource_read_from_json (parsed.value ());
}

result<void, mcp_error>
mcp_client::subscribe_resource (const std::string &uri,
                                std::chrono::milliseconds timeout)
{
  json_builder jb;
  jb.begin_object ();
  jb.add_string ("uri", uri);
  jb.end_object ();
  auto r = call ("resources/subscribe", jb.str (), timeout);
  if (!r) {
    return r.error ();
  }
  return result<void, mcp_error>{};
}

result<void, mcp_error>
mcp_client::unsubscribe_resource (const std::string &uri,
                                  std::chrono::milliseconds timeout)
{
  json_builder jb;
  jb.begin_object ();
  jb.add_string ("uri", uri);
  jb.end_object ();
  auto r = call ("resources/unsubscribe", jb.str (), timeout);
  if (!r) {
    return r.error ();
  }
  return result<void, mcp_error>{};
}

result<prompts_list_result, mcp_error>
mcp_client::list_prompts (const std::optional<std::string> &cursor,
                          std::chrono::milliseconds timeout)
{
  json_builder jb;
  jb.begin_object ();
  jb.add_optional_string ("cursor", cursor);
  jb.end_object ();
  auto r = call ("prompts/list", jb.str (), timeout);
  if (!r) {
    return r.error ();
  }
  auto parsed = parse_json (r.value ());
  if (!parsed) {
    return parsed.error ();
  }
  return prompts_list_from_json (parsed.value ());
}

result<std::vector<prompt>, mcp_error>
mcp_client::list_all_prompts (std::chrono::milliseconds timeout)
{
  std::vector<prompt> out;
  std::optional<std::string> cursor;
  while (true) {
    auto page = list_prompts (cursor, timeout);
    if (!page) {
      return page.error ();
    }
    for (const prompt &p : page.value ().prompts) {
      out.push_back (p);
    }
    if (!page.value ().next_cursor) {
      break;
    }
    cursor = page.value ().next_cursor;
  }
  return out;
}

result<prompt_result, mcp_error>
mcp_client::get_prompt (const std::string &name,
                        const std::optional<std::string> &arguments_json,
                        std::chrono::milliseconds timeout)
{
  json_builder jb;
  jb.begin_object ();
  jb.add_string ("name", name);
  if (arguments_json) {
    jb.add_raw_json ("arguments", *arguments_json);
  }
  jb.end_object ();
  auto r = call ("prompts/get", jb.str (), timeout);
  if (!r) {
    return r.error ();
  }
  auto parsed = parse_json (r.value ());
  if (!parsed) {
    return parsed.error ();
  }
  return prompt_result_from_json (parsed.value ());
}

result<completion_result, mcp_error>
mcp_client::complete (const completion_params &params,
                      std::chrono::milliseconds timeout)
{
  auto r = call ("completion/complete", to_json (params), timeout);
  if (!r) {
    return r.error ();
  }
  auto parsed = parse_json (r.value ());
  if (!parsed) {
    return parsed.error ();
  }
  return completion_result_from_json (parsed.value ());
}

result<void, mcp_error>
mcp_client::set_log_level (log_level level, std::chrono::milliseconds timeout)
{
  json_builder jb;
  jb.begin_object ();
  jb.add_string ("level", log_level_name (level));
  jb.end_object ();
  auto r = call ("logging/setLevel", jb.str (), timeout);
  if (!r) {
    return r.error ();
  }
  return result<void, mcp_error>{};
}

void
mcp_client::notify_roots_changed ()
{
  notify ("notifications/roots/list_changed", "{}");
}

void
mcp_client::notify_progress (const progress_notification &n)
{
  notify ("notifications/progress", to_json (n));
}

void
mcp_client::set_sampling_handler (sampling_handler handler)
{
  std::lock_guard<std::mutex> lock (m_handler_mutex);
  m_on_sampling = std::move (handler);
}

void
mcp_client::set_elicitation_handler (elicitation_handler handler)
{
  std::lock_guard<std::mutex> lock (m_handler_mutex);
  m_on_elicitation = std::move (handler);
}

void
mcp_client::set_roots (const std::vector<root> &roots)
{
  std::lock_guard<std::mutex> lock (m_handler_mutex);
  m_roots = roots;
}

void
mcp_client::set_log_handler (log_handler handler)
{
  std::lock_guard<std::mutex> lock (m_handler_mutex);
  m_on_log = std::move (handler);
}

void
mcp_client::set_tools_changed_handler (std::function<void ()> handler)
{
  std::lock_guard<std::mutex> lock (m_handler_mutex);
  m_on_tools_changed = std::move (handler);
}

void
mcp_client::set_resources_changed_handler (std::function<void ()> handler)
{
  std::lock_guard<std::mutex> lock (m_handler_mutex);
  m_on_resources_changed = std::move (handler);
}

void
mcp_client::set_resource_updated_handler (
    std::function<void (const std::string &)> handler)
{
  std::lock_guard<std::mutex> lock (m_handler_mutex);
  m_on_resource_updated = std::move (handler);
}

void
mcp_client::set_prompts_changed_handler (std::function<void ()> handler)
{
  std::lock_guard<std::mutex> lock (m_handler_mutex);
  m_on_prompts_changed = std::move (handler);
}

void
mcp_client::set_progress_handler (progress_handler handler)
{
  std::lock_guard<std::mutex> lock (m_handler_mutex);
  m_on_progress = std::move (handler);
}

void
mcp_client::set_cancelled_handler (cancelled_handler handler)
{
  std::lock_guard<std::mutex> lock (m_handler_mutex);
  m_on_cancelled = std::move (handler);
}

void
mcp_client::on_line (const std::string &line)
{
  dispatch_message (line);
}

void
mcp_client::dispatch_message (const std::string &line)
{
  thread_local simdjson::dom::parser parser;
  auto doc = parser.parse (line);
  if (doc.error ()) {
    spdlog::warn ("[mcp] Invalid JSON message");
    return;
  }

  auto jsonrpc = doc["jsonrpc"];
  std::string_view version;
  if (jsonrpc.error () || jsonrpc.get_string ().get (version) != 0
      || version != "2.0") {
    spdlog::warn ("[mcp] Invalid JSON-RPC message");
    return;
  }

  auto id_el = doc["id"];
  auto method_el = doc["method"];

  if (!id_el.error () && !method_el.error ()) {
    // Server-initiated request: queue for the dispatch thread so the
    // read loop keeps flowing while handlers run.
    incoming_request request;
    request.id_json = simdjson::to_string (id_el.value ());
    request.method = std::string (method_el.get_string ().value ());
    auto params_el = doc["params"];
    if (!params_el.error ()) {
      request.params = simdjson::to_string (params_el.value ());
    } else {
      request.params = "{}";
    }
    {
      std::lock_guard<std::mutex> lock (m_dispatch_mutex);
      m_dispatch_queue.push_back (std::move (request));
    }
    m_dispatch_cv.notify_one ();
    return;
  }

  if (!method_el.error ()) {
    std::string method (method_el.get_string ().value ());
    std::string params = "{}";
    auto params_el = doc["params"];
    if (!params_el.error ()) {
      params = simdjson::to_string (params_el.value ());
    }
    handle_notification (method, params);
    return;
  }

  spdlog::warn ("[mcp] Malformed JSON-RPC message");
}

void
mcp_client::handle_notification (const std::string &method,
                                 const std::string &params)
{
  if (method == "notifications/message") {
    auto parsed = parse_json (params);
    if (!parsed) {
      return;
    }
    auto msg = log_message_from_json (parsed.value ());
    if (!msg) {
      return;
    }
    std::lock_guard<std::mutex> lock (m_handler_mutex);
    if (m_on_log) {
      m_on_log (msg.value ());
    }
    return;
  }
  if (method == "notifications/tools/list_changed") {
    std::lock_guard<std::mutex> lock (m_handler_mutex);
    if (m_on_tools_changed) {
      m_on_tools_changed ();
    }
    return;
  }
  if (method == "notifications/resources/list_changed") {
    std::lock_guard<std::mutex> lock (m_handler_mutex);
    if (m_on_resources_changed) {
      m_on_resources_changed ();
    }
    return;
  }
  if (method == "notifications/resources/updated") {
    auto parsed = parse_json (params);
    std::string uri;
    if (parsed) {
      auto u = parsed.value ()["uri"].get_string ();
      if (!u.error ()) {
        uri = std::string (u.value ());
      }
    }
    std::lock_guard<std::mutex> lock (m_handler_mutex);
    if (m_on_resource_updated) {
      m_on_resource_updated (uri);
    }
    return;
  }
  if (method == "notifications/prompts/list_changed") {
    std::lock_guard<std::mutex> lock (m_handler_mutex);
    if (m_on_prompts_changed) {
      m_on_prompts_changed ();
    }
    return;
  }
  if (method == "notifications/progress") {
    auto parsed = parse_json (params);
    if (!parsed) {
      return;
    }
    auto n = progress_notification_from_json (parsed.value ());
    if (!n) {
      return;
    }
    std::lock_guard<std::mutex> lock (m_handler_mutex);
    if (m_on_progress) {
      m_on_progress (n.value ());
    }
    return;
  }
  if (method == "notifications/cancelled") {
    auto parsed = parse_json (params);
    wire_id id{ int64_t{ 0 } };
    std::string reason;
    if (parsed) {
      auto id_el = parsed.value ()["requestId"];
      if (!id_el.error ()) {
        int64_t as_int = 0;
        if (id_el.value ().get_int64 ().get (as_int) == 0) {
          id = as_int;
        } else {
          auto as_str = id_el.value ().get_string ();
          if (!as_str.error ()) {
            id = std::string (as_str.value ());
          }
        }
      }
      auto r = parsed.value ()["reason"].get_string ();
      if (!r.error ()) {
        reason = std::string (r.value ());
      }
    }
    std::lock_guard<std::mutex> lock (m_handler_mutex);
    if (m_on_cancelled) {
      m_on_cancelled (id, reason);
    }
    return;
  }
}

void
mcp_client::start_dispatch_thread ()
{
  if (!m_dispatch_thread.joinable ()) {
    m_dispatch_thread = std::thread ([this] { process_dispatch_queue (); });
  }
}

void
mcp_client::stop_dispatch_thread ()
{
  m_dispatch_cv.notify_all ();
  if (m_dispatch_thread.joinable ()) {
    m_dispatch_thread.join ();
  }
}

void
mcp_client::process_dispatch_queue ()
{
  while (true) {
    incoming_request request;
    {
      std::unique_lock<std::mutex> lock (m_dispatch_mutex);
      m_dispatch_cv.wait (lock, [this] {
        return !m_running.load () || !m_dispatch_queue.empty ();
      });
      if (!m_running.load () && m_dispatch_queue.empty ()) {
        break;
      }
      request = std::move (m_dispatch_queue.front ());
      m_dispatch_queue.pop_front ();
    }
    handle_incoming_request (request);
  }
}

void
mcp_client::handle_incoming_request (const incoming_request &request)
{
  if (request.method == "ping") {
    write_line (envelope_result (request.id_json, "{}"));
    return;
  }

  if (request.method == "roots/list") {
    std::vector<root> roots;
    {
      std::lock_guard<std::mutex> lock (m_handler_mutex);
      roots = m_roots;
    }
    json_builder jb;
    jb.begin_object ();
    jb.begin_array ("roots");
    for (const root &r : roots) {
      jb.add_array_raw_json (to_json (r));
    }
    jb.end_array ();
    jb.end_object ();
    write_line (envelope_result (request.id_json, jb.str ()));
    return;
  }

  if (request.method == "sampling/createMessage") {
    sampling_handler handler;
    {
      std::lock_guard<std::mutex> lock (m_handler_mutex);
      handler = m_on_sampling;
    }
    if (!handler) {
      write_line (envelope_error (request.id_json, METHOD_NOT_FOUND,
                                  "Sampling not supported by client"));
      return;
    }
    auto parsed = parse_json (request.params);
    if (!parsed) {
      write_line (envelope_error (request.id_json, INVALID_PARAMS,
                                  "Invalid sampling params"));
      return;
    }
    auto params = sampling_params_from_json (parsed.value ());
    if (!params) {
      write_line (envelope_error (request.id_json, INVALID_PARAMS,
                                  params.error ().message));
      return;
    }
    auto r = handler (params.value ());
    if (!r) {
      write_line (envelope_error (request.id_json,
                                  static_cast<int64_t> (r.error ().code),
                                  r.error ().message));
      return;
    }
    write_line (envelope_result (request.id_json, to_json (r.value ())));
    return;
  }

  if (request.method == "elicitation/create") {
    elicitation_handler handler;
    {
      std::lock_guard<std::mutex> lock (m_handler_mutex);
      handler = m_on_elicitation;
    }
    if (!handler) {
      write_line (envelope_error (request.id_json, METHOD_NOT_FOUND,
                                  "Elicitation not supported by client"));
      return;
    }
    auto parsed = parse_json (request.params);
    if (!parsed) {
      write_line (envelope_error (request.id_json, INVALID_PARAMS,
                                  "Invalid elicitation params"));
      return;
    }
    auto params = elicitation_params_from_json (parsed.value ());
    if (!params) {
      write_line (envelope_error (request.id_json, INVALID_PARAMS,
                                  params.error ().message));
      return;
    }
    auto r = handler (params.value ());
    if (!r) {
      write_line (envelope_error (request.id_json,
                                  static_cast<int64_t> (r.error ().code),
                                  r.error ().message));
      return;
    }
    write_line (envelope_result (request.id_json, to_json (r.value ())));
    return;
  }

  spdlog::warn ("[mcp] Unsupported server request: {}", request.method);
  write_line (
      envelope_error (request.id_json, METHOD_NOT_FOUND,
                      "Method not handled by client: " + request.method));
}

} // namespace agentsdk::mcp
