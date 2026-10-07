#include <agentsdk/mcp/mcp_server.hpp>

#include <agentsdk/common/json_builder.hpp>

#include <spdlog/spdlog.h>

#include <sys/select.h>
#include <unistd.h>

#include <cerrno>
#include <iostream>

namespace agentsdk::mcp
{

namespace
{

// Unbuffered stdin line reader shared by run() and the send_request pump
// (both live on the same thread).  iostream buffering must not be used
// here: a buffered-ahead line would be invisible to select() and could
// stall the pump while data is already available.
bool
stdin_read_line (std::string &out,
                 const std::chrono::steady_clock::time_point &deadline,
                 bool infinite)
{
  thread_local std::string buffer;

  while (true) {
    auto pos = buffer.find ('\n');
    if (pos != std::string::npos) {
      out = buffer.substr (0, pos);
      buffer.erase (0, pos + 1);
      return true;
    }

    fd_set fds;
    FD_ZERO (&fds);
    FD_SET (STDIN_FILENO, &fds);
    struct timeval tv;
    struct timeval *tvp = nullptr;
    if (!infinite) {
      auto remaining = deadline - std::chrono::steady_clock::now ();
      if (remaining <= std::chrono::steady_clock::duration::zero ()) {
        return false; // Timeout.
      }
      auto us
          = std::chrono::duration_cast<std::chrono::microseconds> (remaining);
      tv.tv_sec = static_cast<long> (us.count () / 1000000);
      tv.tv_usec = static_cast<long> (us.count () % 1000000);
      tvp = &tv;
    }
    int ready = ::select (STDIN_FILENO + 1, &fds, nullptr, nullptr, tvp);
    if (ready < 0) {
      if (errno == EINTR) {
        continue;
      }
      return false;
    }
    if (ready == 0) {
      return false; // Timeout.
    }

    char chunk[4096];
    ssize_t n = ::read (STDIN_FILENO, chunk, sizeof (chunk));
    if (n <= 0) {
      return false; // EOF or error.
    }
    buffer.append (chunk, static_cast<size_t> (n));
  }
}

} // namespace

mcp_server::mcp_server (implementation_info info, server_capabilities caps)
    : m_info (std::move (info)), m_caps (std::move (caps))
{
}

void
mcp_server::run ()
{
  std::string line;
  const auto no_deadline = std::chrono::steady_clock::time_point{};
  while (m_running.load ()) {
    if (!stdin_read_line (line, no_deadline, true)) {
      break; // EOF or error: the client is gone.
    }
    if (!line.empty () && line.back () == '\r') {
      line.pop_back ();
    }
    if (line.empty ()) {
      continue;
    }
    auto response = handle_line (line);
    if (response) {
      send_line (*response);
    }
  }
}

void
mcp_server::request_stop ()
{
  m_running.store (false);
}

std::optional<std::string>
mcp_server::handle_line (const std::string &line)
{
  thread_local simdjson::dom::parser parser;
  auto doc = parser.parse (line);
  if (doc.error ()) {
    spdlog::warn ("[mcp-server] Invalid JSON message");
    return std::nullopt;
  }

  auto id_el = doc["id"];
  auto method_el = doc["method"];

  if (!id_el.error () && method_el.error ()) {
    // Response to one of our outgoing requests.
    complete_outgoing (line);
    return std::nullopt;
  }

  if (!method_el.error () && !id_el.error ()) {
    std::string id_json = simdjson::to_string (id_el.value ());
    std::string method (method_el.get_string ().value ());
    std::string params = "{}";
    auto params_el = doc["params"];
    if (!params_el.error ()) {
      params = simdjson::to_string (params_el.value ());
    }
    return handle_request (id_json, method, params);
  }

  if (!method_el.error ()) {
    std::string method (method_el.get_string ().value ());
    std::string params = "{}";
    auto params_el = doc["params"];
    if (!params_el.error ()) {
      params = simdjson::to_string (params_el.value ());
    }
    handle_notification_msg (method, params);
    return std::nullopt;
  }

  spdlog::warn ("[mcp-server] Malformed JSON-RPC message");
  return std::nullopt;
}

std::optional<std::string>
mcp_server::handle_request (const std::string &id_json,
                            const std::string &method,
                            const std::string &params)
{
  // ── Lifecycle ──
  if (method == "initialize") {
    auto parsed = parse_json (params);
    if (!parsed) {
      return envelope_error (id_json, INVALID_PARAMS, "Invalid params");
    }
    simdjson::dom::element obj = parsed.value ();

    std::string requested;
    auto v = obj["protocolVersion"].get_string ();
    if (!v.error ()) {
      requested = std::string (v.value ());
    }
    std::string negotiated = supported_protocol_versions ().front ();
    if (is_supported_version (requested)) {
      negotiated = requested;
    }

    client_capabilities caps;
    auto caps_el = obj["capabilities"];
    if (!caps_el.error ()) {
      auto c = client_capabilities_from_json (caps_el.value ());
      if (c) {
        caps = c.value ();
      }
    }
    implementation_info info;
    auto info_el = obj["clientInfo"];
    if (!info_el.error ()) {
      auto i = implementation_info_from_json (info_el.value ());
      if (i) {
        info = i.value ();
      }
    }

    {
      std::lock_guard<std::mutex> lock (m_state_mutex);
      m_protocol_version = negotiated;
      m_client_caps = caps;
      m_client_info = info;
      m_got_initialize = true;
      m_operation = false;
    }

    json_builder jb;
    jb.begin_object ();
    jb.add_string ("protocolVersion", negotiated);
    jb.add_raw_json ("capabilities", to_json (m_caps));
    jb.add_raw_json ("serverInfo", to_json (m_info));
    {
      std::lock_guard<std::mutex> lock (m_state_mutex);
      jb.add_optional_string ("instructions", m_instructions);
    }
    jb.end_object ();
    return envelope_result (id_json, jb.str ());
  }

  if (method == "ping") {
    return envelope_result (id_json, "{}");
  }

  // Everything below requires the initialize handshake first.
  {
    std::lock_guard<std::mutex> lock (m_state_mutex);
    if (!m_got_initialize) {
      return envelope_error (id_json, INVALID_REQUEST,
                             "Initialize first: send initialize request");
    }
  }

  if (method == "tools/list") {
    list_tools_handler handler;
    {
      std::lock_guard<std::mutex> lock (m_handler_mutex);
      handler = m_on_list_tools;
    }
    if (!handler) {
      return envelope_error (id_json, METHOD_NOT_FOUND, "No tools capability");
    }
    std::optional<std::string> cursor;
    auto parsed = parse_json (params);
    if (parsed) {
      auto c = parsed.value ()["cursor"].get_string ();
      if (!c.error ()) {
        cursor = std::string (c.value ());
      }
    }
    auto r = handler (cursor);
    if (!r) {
      return envelope_error (id_json, r.error ().code, r.error ().message);
    }
    json_builder jb;
    jb.begin_object ();
    jb.begin_array ("tools");
    for (const tool &t : r.value ().tools) {
      jb.add_array_raw_json (to_json (t));
    }
    jb.end_array ();
    jb.add_optional_string ("nextCursor", r.value ().next_cursor);
    jb.end_object ();
    return envelope_result (id_json, jb.str ());
  }

  if (method == "tools/call") {
    call_tool_handler handler;
    {
      std::lock_guard<std::mutex> lock (m_handler_mutex);
      handler = m_on_call_tool;
    }
    if (!handler) {
      return envelope_error (id_json, METHOD_NOT_FOUND, "No tools capability");
    }
    auto parsed = parse_json (params);
    if (!parsed) {
      return envelope_error (id_json, INVALID_PARAMS, "Invalid params");
    }
    auto name_el = parsed.value ()["name"].get_string ();
    if (name_el.error ()) {
      return envelope_error (id_json, INVALID_PARAMS, "Missing tool name");
    }
    std::string name (name_el.value ());
    std::string args = "{}";
    auto args_el = parsed.value ()["arguments"];
    if (!args_el.error ()) {
      args = simdjson::to_string (args_el.value ());
    }
    auto r = handler (name, args);
    if (!r) {
      return envelope_error (id_json, r.error ().code, r.error ().message);
    }
    return envelope_result (id_json, to_json (r.value ()));
  }

  if (method == "resources/list") {
    list_resources_handler handler;
    {
      std::lock_guard<std::mutex> lock (m_handler_mutex);
      handler = m_on_list_resources;
    }
    if (!handler) {
      return envelope_error (id_json, METHOD_NOT_FOUND,
                             "No resources capability");
    }
    std::optional<std::string> cursor;
    auto parsed = parse_json (params);
    if (parsed) {
      auto c = parsed.value ()["cursor"].get_string ();
      if (!c.error ()) {
        cursor = std::string (c.value ());
      }
    }
    auto r = handler (cursor);
    if (!r) {
      return envelope_error (id_json, r.error ().code, r.error ().message);
    }
    json_builder jb;
    jb.begin_object ();
    jb.begin_array ("resources");
    for (const resource &res : r.value ().resources) {
      jb.add_array_raw_json (to_json (res));
    }
    jb.end_array ();
    jb.add_optional_string ("nextCursor", r.value ().next_cursor);
    jb.end_object ();
    return envelope_result (id_json, jb.str ());
  }

  if (method == "resources/templates/list") {
    list_templates_handler handler;
    {
      std::lock_guard<std::mutex> lock (m_handler_mutex);
      handler = m_on_list_templates;
    }
    if (!handler) {
      return envelope_error (id_json, METHOD_NOT_FOUND,
                             "No resources capability");
    }
    std::optional<std::string> cursor;
    auto parsed = parse_json (params);
    if (parsed) {
      auto c = parsed.value ()["cursor"].get_string ();
      if (!c.error ()) {
        cursor = std::string (c.value ());
      }
    }
    auto r = handler (cursor);
    if (!r) {
      return envelope_error (id_json, r.error ().code, r.error ().message);
    }
    json_builder jb;
    jb.begin_object ();
    jb.begin_array ("resourceTemplates");
    for (const resource_template &t : r.value ().templates) {
      jb.add_array_raw_json (to_json (t));
    }
    jb.end_array ();
    jb.add_optional_string ("nextCursor", r.value ().next_cursor);
    jb.end_object ();
    return envelope_result (id_json, jb.str ());
  }

  if (method == "resources/read") {
    read_resource_handler handler;
    {
      std::lock_guard<std::mutex> lock (m_handler_mutex);
      handler = m_on_read_resource;
    }
    if (!handler) {
      return envelope_error (id_json, METHOD_NOT_FOUND,
                             "No resources capability");
    }
    auto parsed = parse_json (params);
    if (!parsed) {
      return envelope_error (id_json, INVALID_PARAMS, "Invalid params");
    }
    auto uri_el = parsed.value ()["uri"].get_string ();
    if (uri_el.error ()) {
      return envelope_error (id_json, INVALID_PARAMS, "Missing uri");
    }
    auto r = handler (std::string (uri_el.value ()));
    if (!r) {
      return envelope_error (id_json, r.error ().code, r.error ().message);
    }
    json_builder jb;
    jb.begin_object ();
    jb.begin_array ("contents");
    for (const resource_contents &c : r.value ().contents) {
      jb.add_array_raw_json (to_json (c));
    }
    jb.end_array ();
    jb.end_object ();
    return envelope_result (id_json, jb.str ());
  }

  if (method == "resources/subscribe" || method == "resources/unsubscribe") {
    subscribe_handler handler;
    {
      std::lock_guard<std::mutex> lock (m_handler_mutex);
      handler = (method == "resources/subscribe") ? m_on_subscribe
                                                  : m_on_unsubscribe;
    }
    auto parsed = parse_json (params);
    if (!parsed) {
      return envelope_error (id_json, INVALID_PARAMS, "Invalid params");
    }
    auto uri_el = parsed.value ()["uri"].get_string ();
    if (uri_el.error ()) {
      return envelope_error (id_json, INVALID_PARAMS, "Missing uri");
    }
    if (handler) {
      auto r = handler (std::string (uri_el.value ()));
      if (!r) {
        return envelope_error (id_json, r.error ().code, r.error ().message);
      }
    }
    return envelope_result (id_json, "{}");
  }

  if (method == "prompts/list") {
    list_prompts_handler handler;
    {
      std::lock_guard<std::mutex> lock (m_handler_mutex);
      handler = m_on_list_prompts;
    }
    if (!handler) {
      return envelope_error (id_json, METHOD_NOT_FOUND,
                             "No prompts capability");
    }
    std::optional<std::string> cursor;
    auto parsed = parse_json (params);
    if (parsed) {
      auto c = parsed.value ()["cursor"].get_string ();
      if (!c.error ()) {
        cursor = std::string (c.value ());
      }
    }
    auto r = handler (cursor);
    if (!r) {
      return envelope_error (id_json, r.error ().code, r.error ().message);
    }
    json_builder jb;
    jb.begin_object ();
    jb.begin_array ("prompts");
    for (const prompt &p : r.value ().prompts) {
      jb.add_array_raw_json (to_json (p));
    }
    jb.end_array ();
    jb.add_optional_string ("nextCursor", r.value ().next_cursor);
    jb.end_object ();
    return envelope_result (id_json, jb.str ());
  }

  if (method == "prompts/get") {
    get_prompt_handler handler;
    {
      std::lock_guard<std::mutex> lock (m_handler_mutex);
      handler = m_on_get_prompt;
    }
    if (!handler) {
      return envelope_error (id_json, METHOD_NOT_FOUND,
                             "No prompts capability");
    }
    auto parsed = parse_json (params);
    if (!parsed) {
      return envelope_error (id_json, INVALID_PARAMS, "Invalid params");
    }
    auto name_el = parsed.value ()["name"].get_string ();
    if (name_el.error ()) {
      return envelope_error (id_json, INVALID_PARAMS, "Missing prompt name");
    }
    std::optional<std::string> args;
    auto args_el = parsed.value ()["arguments"];
    if (!args_el.error ()) {
      args = simdjson::to_string (args_el.value ());
    }
    auto r = handler (std::string (name_el.value ()), args);
    if (!r) {
      return envelope_error (id_json, r.error ().code, r.error ().message);
    }
    return envelope_result (id_json, to_json (r.value ()));
  }

  if (method == "completion/complete") {
    completion_handler handler;
    {
      std::lock_guard<std::mutex> lock (m_handler_mutex);
      handler = m_on_complete;
    }
    if (!handler) {
      return envelope_error (id_json, METHOD_NOT_FOUND,
                             "No completions capability");
    }
    auto parsed = parse_json (params);
    if (!parsed) {
      return envelope_error (id_json, INVALID_PARAMS, "Invalid params");
    }
    auto cp = completion_params_from_json (parsed.value ());
    if (!cp) {
      return envelope_error (id_json, INVALID_PARAMS, cp.error ().message);
    }
    auto r = handler (cp.value ());
    if (!r) {
      return envelope_error (id_json, r.error ().code, r.error ().message);
    }
    return envelope_result (id_json, to_json (r.value ()));
  }

  if (method == "logging/setLevel") {
    auto parsed = parse_json (params);
    if (!parsed) {
      return envelope_error (id_json, INVALID_PARAMS, "Invalid params");
    }
    auto level_el = parsed.value ()["level"].get_string ();
    if (level_el.error ()) {
      return envelope_error (id_json, INVALID_PARAMS, "Missing level");
    }
    auto level = parse_log_level (std::string (level_el.value ()));
    if (!level) {
      return envelope_error (id_json, INVALID_PARAMS, "Unknown log level");
    }
    {
      std::lock_guard<std::mutex> lock (m_state_mutex);
      m_log_threshold = *level;
    }
    return envelope_result (id_json, "{}");
  }

  return envelope_error (id_json, METHOD_NOT_FOUND,
                         "Method not handled by server: " + method);
}

void
mcp_server::handle_notification_msg (const std::string &method,
                                     const std::string &params)
{
  (void)params;
  if (method == "notifications/initialized") {
    std::lock_guard<std::mutex> lock (m_state_mutex);
    m_operation = true;
    return;
  }
  if (method == "notifications/roots/list_changed") {
    std::function<void ()> handler;
    {
      std::lock_guard<std::mutex> lock (m_handler_mutex);
      handler = m_on_roots_changed;
    }
    if (handler) {
      handler ();
    }
    return;
  }
  // Progress and cancellation notifications from the client are accepted
  // and ignored: this server has no long-running outgoing requests that
  // would need them today.
}

void
mcp_server::complete_outgoing (const std::string &line)
{
  auto parsed = parse_json (line);
  if (!parsed) {
    return;
  }
  simdjson::dom::element doc = parsed.value ();
  auto id_el = doc["id"];
  if (id_el.error ()) {
    return;
  }
  int64_t id = 0;
  if (id_el.value ().get_int64 ().get (id) != 0) {
    return;
  }
  std::string result_str;
  std::string error_str;
  auto result_el = doc["result"];
  if (!result_el.error ()) {
    result_str = simdjson::to_string (result_el.value ());
  }
  auto error_el = doc["error"];
  if (!error_el.error ()) {
    error_str = simdjson::to_string (error_el.value ());
  }
  std::lock_guard<std::mutex> lock (m_pending_mutex);
  auto it = m_pending.find (id);
  if (it != m_pending.end ()) {
    it->second->result = std::move (result_str);
    it->second->error = std::move (error_str);
    it->second->completed = true;
  }
}

result<std::string, mcp_error>
mcp_server::send_request (const std::string &method,
                          const std::string &params_json,
                          std::chrono::milliseconds timeout)
{
  {
    std::lock_guard<std::mutex> lock (m_state_mutex);
    if (!m_operation) {
      return mcp_error{ INVALID_REQUEST,
                        "Cannot send requests before initialized",
                        std::nullopt };
    }
  }

  int64_t id = m_next_id.fetch_add (1);
  json_builder jb;
  jb.begin_object ();
  jb.add_string ("jsonrpc", "2.0");
  jb.add_int ("id", id);
  jb.add_string ("method", method);
  jb.add_raw_json ("params", params_json);
  jb.end_object ();

  auto pending = std::make_shared<pending_request> ();
  {
    std::lock_guard<std::mutex> lock (m_pending_mutex);
    m_pending[id] = pending;
  }

  send_line (jb.str ());

  // Pump the stdio reader while waiting: the run loop lives on this very
  // thread, so a plain condition wait would deadlock (nothing would read
  // the client's reply).  Each line is routed through handle_line, which
  // completes this pending request or answers the client's own requests.
  auto deadline = std::chrono::steady_clock::now () + timeout;
  const bool infinite = timeout <= std::chrono::milliseconds::zero ();
  while (true) {
    {
      std::lock_guard<std::mutex> lock (m_pending_mutex);
      if (pending->completed) {
        break;
      }
    }

    std::string line;
    if (!stdin_read_line (line, deadline, infinite)) {
      break; // Timeout or EOF: the client is gone.
    }
    if (!line.empty () && line.back () == '\r') {
      line.pop_back ();
    }
    if (line.empty ()) {
      continue;
    }
    auto response = handle_line (line);
    if (response) {
      send_line (*response);
    }
  }

  std::string result_json;
  std::string error_json;
  bool completed = false;
  {
    std::lock_guard<std::mutex> lock (m_pending_mutex);
    result_json = pending->result;
    error_json = pending->error;
    completed = pending->completed;
    m_pending.erase (id);
  }

  if (!completed) {
    return mcp_error{ INTERNAL_ERROR, "Request timed out", std::nullopt };
  }
  if (!error_json.empty ()) {
    return parse_rpc_error (error_json);
  }
  return result<std::string, mcp_error>{ result_json };
}

void
mcp_server::send_line (const std::string &line)
{
  std::lock_guard<std::mutex> lock (m_write_mutex);
  std::cout << line << '\n';
  std::cout.flush ();
}

void
mcp_server::set_list_tools_handler (list_tools_handler handler)
{
  std::lock_guard<std::mutex> lock (m_handler_mutex);
  m_on_list_tools = std::move (handler);
}

void
mcp_server::set_call_tool_handler (call_tool_handler handler)
{
  std::lock_guard<std::mutex> lock (m_handler_mutex);
  m_on_call_tool = std::move (handler);
}

void
mcp_server::set_list_resources_handler (list_resources_handler handler)
{
  std::lock_guard<std::mutex> lock (m_handler_mutex);
  m_on_list_resources = std::move (handler);
}

void
mcp_server::set_list_templates_handler (list_templates_handler handler)
{
  std::lock_guard<std::mutex> lock (m_handler_mutex);
  m_on_list_templates = std::move (handler);
}

void
mcp_server::set_read_resource_handler (read_resource_handler handler)
{
  std::lock_guard<std::mutex> lock (m_handler_mutex);
  m_on_read_resource = std::move (handler);
}

void
mcp_server::set_subscribe_handler (subscribe_handler handler)
{
  std::lock_guard<std::mutex> lock (m_handler_mutex);
  m_on_subscribe = std::move (handler);
}

void
mcp_server::set_unsubscribe_handler (subscribe_handler handler)
{
  std::lock_guard<std::mutex> lock (m_handler_mutex);
  m_on_unsubscribe = std::move (handler);
}

void
mcp_server::set_list_prompts_handler (list_prompts_handler handler)
{
  std::lock_guard<std::mutex> lock (m_handler_mutex);
  m_on_list_prompts = std::move (handler);
}

void
mcp_server::set_get_prompt_handler (get_prompt_handler handler)
{
  std::lock_guard<std::mutex> lock (m_handler_mutex);
  m_on_get_prompt = std::move (handler);
}

void
mcp_server::set_completion_handler (completion_handler handler)
{
  std::lock_guard<std::mutex> lock (m_handler_mutex);
  m_on_complete = std::move (handler);
}

void
mcp_server::set_roots_changed_handler (std::function<void ()> handler)
{
  std::lock_guard<std::mutex> lock (m_handler_mutex);
  m_on_roots_changed = std::move (handler);
}

void
mcp_server::set_instructions (const std::string &instructions)
{
  std::lock_guard<std::mutex> lock (m_state_mutex);
  m_instructions = instructions;
}

result<sampling_result, mcp_error>
mcp_server::request_sampling (const sampling_params &params,
                              std::chrono::milliseconds timeout)
{
  auto r = send_request ("sampling/createMessage", to_json (params), timeout);
  if (!r) {
    return r.error ();
  }
  auto parsed = parse_json (r.value ());
  if (!parsed) {
    return parsed.error ();
  }
  return sampling_result_from_json (parsed.value ());
}

result<roots_list_result, mcp_error>
mcp_server::request_roots (std::chrono::milliseconds timeout)
{
  auto r = send_request ("roots/list", "{}", timeout);
  if (!r) {
    return r.error ();
  }
  auto parsed = parse_json (r.value ());
  if (!parsed) {
    return parsed.error ();
  }
  return roots_list_from_json (parsed.value ());
}

result<elicitation_result, mcp_error>
mcp_server::request_elicitation (const elicitation_params &params,
                                 std::chrono::milliseconds timeout)
{
  auto r = send_request ("elicitation/create", to_json (params), timeout);
  if (!r) {
    return r.error ();
  }
  auto parsed = parse_json (r.value ());
  if (!parsed) {
    return parsed.error ();
  }
  return elicitation_result_from_json (parsed.value ());
}

void
mcp_server::emit_log (log_level level, const std::string &data_json,
                      const std::optional<std::string> &logger)
{
  {
    std::lock_guard<std::mutex> lock (m_state_mutex);
    if (!log_level_at_least (level, m_log_threshold)) {
      return;
    }
  }
  log_message msg;
  msg.level = level;
  msg.logger = logger;
  msg.data = data_json;
  json_builder jb;
  jb.begin_object ();
  jb.add_string ("jsonrpc", "2.0");
  jb.add_string ("method", "notifications/message");
  jb.add_raw_json ("params", to_json (msg));
  jb.end_object ();
  send_line (jb.str ());
}

namespace
{

std::string
empty_notification (const std::string &method)
{
  json_builder jb;
  jb.begin_object ();
  jb.add_string ("jsonrpc", "2.0");
  jb.add_string ("method", method);
  jb.begin_object ("params");
  jb.end_object ();
  jb.end_object ();
  return jb.str ();
}

} // namespace

void
mcp_server::notify_tools_changed ()
{
  send_line (empty_notification ("notifications/tools/list_changed"));
}

void
mcp_server::notify_resources_changed ()
{
  send_line (empty_notification ("notifications/resources/list_changed"));
}

void
mcp_server::notify_resource_updated (const std::string &uri)
{
  json_builder jb;
  jb.begin_object ();
  jb.add_string ("jsonrpc", "2.0");
  jb.add_string ("method", "notifications/resources/updated");
  jb.begin_object ("params");
  jb.add_string ("uri", uri);
  jb.end_object ();
  jb.end_object ();
  send_line (jb.str ());
}

void
mcp_server::notify_prompts_changed ()
{
  send_line (empty_notification ("notifications/prompts/list_changed"));
}

void
mcp_server::notify_progress (const progress_notification &n)
{
  json_builder jb;
  jb.begin_object ();
  jb.add_string ("jsonrpc", "2.0");
  jb.add_string ("method", "notifications/progress");
  jb.add_raw_json ("params", to_json (n));
  jb.end_object ();
  send_line (jb.str ());
}

std::string
mcp_server::protocol_version () const
{
  std::lock_guard<std::mutex> lock (m_state_mutex);
  return m_protocol_version;
}

client_capabilities
mcp_server::client_caps () const
{
  std::lock_guard<std::mutex> lock (m_state_mutex);
  return m_client_caps;
}

implementation_info
mcp_server::client_info () const
{
  std::lock_guard<std::mutex> lock (m_state_mutex);
  return m_client_info;
}

} // namespace agentsdk::mcp
