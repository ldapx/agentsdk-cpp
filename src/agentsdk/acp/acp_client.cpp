#include <agentsdk/a2a/json_util.hpp>

#include <agentsdk/acp/acp_client.hpp>

#include <spdlog/spdlog.h>

namespace agentsdk::acp
{

acp_client::acp_client () = default;

acp_client::~acp_client () { terminate (); }

bool
acp_client::launch_agent (const std::string &command,
                          const std::vector<std::string> &args)
{
  bool ok = launch (command, args);
  if (ok) {
    start_dispatch_thread ();
  }
  return ok;
}

std::string
acp_client::send_request (const std::string &method,
                          const std::string &params_json,
                          std::chrono::milliseconds timeout)
{
  return request (method, params_json, timeout);
}

void
acp_client::send_notification (const std::string &method,
                               const std::string &params_json)
{
  notify (method, params_json);
}

void
acp_client::set_notification_handler (notification_handler handler)
{
  m_on_notification = std::move (handler);
}

void
acp_client::set_agent_request_handler (
    std::function<agent_response (const std::string &method,
                                  const std::string &params)>
        handler)
{
  m_on_agent_request = std::move (handler);
}

bool
acp_client::is_running () const
{
  return stdio_json_rpc::is_running ();
}

void
acp_client::terminate ()
{
  // The dispatch thread must be stopped before the transport tears down: it
  // answers agent requests, and it only exits once m_running is false.
  m_running.store (false);
  stop_dispatch_thread ();

  stdio_json_rpc::terminate ();
}

void
acp_client::on_line (const std::string &line)
{
  dispatch_message (line);
}

void
acp_client::dispatch_message (const std::string &line)
{
  thread_local simdjson::dom::parser parser;
  auto doc = parser.parse (line);

  // Extract jsonrpc version
  auto jsonrpc = doc["jsonrpc"];
  std::string_view jsonrpc_val;
  if (jsonrpc.error () || jsonrpc.get_string ().get (jsonrpc_val) != 0
      || jsonrpc_val != "2.0") {
    spdlog::warn ("[acp] Invalid JSON-RPC message");
    return;
  }

  auto id_el = doc["id"];
  auto method_el = doc["method"];

  if (!id_el.error () && !method_el.error ()) {
    // Request from the agent (permission prompts, fs access, ...).
    // Queue it for the dispatch thread so the read loop keeps
    // processing messages while the handler runs.
    incoming_request request;
    request.id_json = simdjson::to_string (id_el.value ());
    request.method = std::string (method_el.get_string ().value ());

    auto params_el = doc["params"];
    if (!params_el.error ()) {
      request.params = simdjson::to_string (params_el.value ());
    }

    spdlog::info ("[acp] Agent request: {}", request.method);

    {
      std::lock_guard<std::mutex> lock (m_dispatch_mutex);
      m_dispatch_queue.push_back (std::move (request));
    }
    m_dispatch_cv.notify_one ();
    return;
  }

  if (!method_el.error ()) {
    // Notification from agent
    std::string method (method_el.get_string ().value ());
    std::string params;
    auto params_el = doc["params"];
    if (!params_el.error ()) {
      params = simdjson::to_string (params_el.value ());
    }

    if (m_on_notification) {
      m_on_notification (method, params);
    }
    return;
  }

  spdlog::warn ("[acp] Malformed JSON-RPC message");
}

void
acp_client::start_dispatch_thread ()
{
  if (!m_dispatch_thread.joinable ()) {
    m_dispatch_thread = std::thread ([this] { process_dispatch_queue (); });
  }
}

void
acp_client::stop_dispatch_thread ()
{
  // m_running is false by now; wake the loop so it can exit.
  m_dispatch_cv.notify_all ();

  if (m_dispatch_thread.joinable ()) {
    m_dispatch_thread.join ();
  }
}

void
acp_client::process_dispatch_queue ()
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
acp_client::handle_incoming_request (const incoming_request &request)
{
  agent_response response;
  if (m_on_agent_request) {
    response = m_on_agent_request (request.method, request.params);
  }

  if (!response.handled) {
    spdlog::warn ("[acp] Unhandled agent request: {}", request.method);
  }

  json_builder jb;
  jb.begin_object ();
  jb.add_string ("jsonrpc", "2.0");
  jb.add_raw_json ("id", request.id_json);

  if (response.handled && response.ok) {
    jb.add_raw_json ("result", response.result_json);
  } else {
    jb.begin_object ("error");
    jb.add_int ("code", response.handled ? response.error_code : -32601);
    jb.add_string ("message",
                   response.handled
                       ? response.error_message
                       : "Method not handled by client: " + request.method);
    jb.end_object ();
  }

  jb.end_object ();
  write_line (jb.str ());
}

} // namespace agentsdk::acp
