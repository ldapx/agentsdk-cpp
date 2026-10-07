#include <agentsdk/a2a/http/json_rpc_server.hpp>

#include <spdlog/spdlog.h>

#include <agentsdk/a2a/http/http_listener.hpp>
#include <agentsdk/a2a/json_util.hpp>

namespace agentsdk::a2a
{

json_rpc_server_transport::json_rpc_server_transport (
    agent_card card, std::shared_ptr<request_handler> handler)
    : m_card (std::move (card)), m_handler (std::move (handler))
{
}

json_rpc_server_transport::~json_rpc_server_transport () { stop (); }

std::string
json_rpc_server_transport::dispatch_rpc (const std::string &request_body)
{
  auto parsed = parse_json (request_body);
  if (!parsed) {
    json_builder jb;
    jb.begin_object ();
    jb.add_string ("jsonrpc", "2.0");
    jb.add_raw_json ("id", "null");
    jb.begin_object ();
    jb.add_int ("code", static_cast<int64_t> (error_code::parse_error));
    jb.add_string ("message", "Parse error");
    jb.end_object ();
    jb.end_object ();
    return jb.str ();
  }

  auto element = parsed.value ();

  // Extract method
  auto method_result = element["method"].get_string ();
  if (method_result.error ()) {
    json_builder jb;
    jb.begin_object ();
    jb.add_string ("jsonrpc", "2.0");
    jb.add_raw_json ("id", "null");
    jb.begin_object ();
    jb.add_int ("code", static_cast<int64_t> (error_code::invalid_request));
    jb.add_string ("message", "Missing method field");
    jb.end_object ();
    jb.end_object ();
    return jb.str ();
  }

  std::string method (method_result.value ());
  std::string id_str;
  auto id_el = element["id"];
  if (!id_el.error ()) {
    auto id_int = id_el.get_int64 ();
    if (!id_int.error ()) {
      id_str = std::to_string (id_int.value ());
    } else {
      auto id_str_r = id_el.get_string ();
      if (!id_str_r.error ()) {
        id_str = "\"" + std::string (id_str_r.value ()) + "\"";
      } else {
        id_str = "null";
      }
    }
  } else {
    id_str = "null";
  }

  // Dispatch based on method name
  auto params = element["params"];

  if (method == "SendMessage") {
    if (params.error ()) {
      json_builder jb;
      jb.begin_object ();
      jb.add_string ("jsonrpc", "2.0");
      jb.add_raw_json ("id", id_str);
      jb.begin_object ();
      jb.add_int ("code", static_cast<int64_t> (error_code::invalid_params));
      jb.add_string ("message", "Missing params");
      jb.end_object ();
      jb.end_object ();
      return jb.str ();
    }

    auto req = send_message_request_from_json (params.value ());
    if (!req) {
      json_builder jb;
      jb.begin_object ();
      jb.add_string ("jsonrpc", "2.0");
      jb.add_raw_json ("id", id_str);
      jb.begin_object ();
      jb.add_int ("code", static_cast<int64_t> (req.error ().code));
      jb.add_string ("message", req.error ().message);
      jb.end_object ();
      jb.end_object ();
      return jb.str ();
    }

    auto result = m_handler->on_send_message (*req);
    if (!result) {
      json_builder jb;
      jb.begin_object ();
      jb.add_string ("jsonrpc", "2.0");
      jb.add_raw_json ("id", id_str);
      jb.begin_object ();
      jb.add_int ("code", static_cast<int64_t> (result.error ().code));
      jb.add_string ("message", result.error ().message);
      jb.end_object ();
      jb.end_object ();
      return jb.str ();
    }

    json_builder jb;
    jb.begin_object ();
    jb.add_string ("jsonrpc", "2.0");
    jb.add_raw_json ("id", id_str);
    jb.begin_object ();
    if (auto *t = std::get_if<task> (&*result)) {
      jb.add_raw_json ("task", to_json (*t));
    } else if (auto *m = std::get_if<message> (&*result)) {
      jb.add_raw_json ("message", to_json (*m));
    }
    jb.end_object ();
    jb.end_object ();
    return jb.str ();
  }

  if (method == "GetTask") {
    if (params.error ()) {
      json_builder jb;
      jb.begin_object ();
      jb.add_string ("jsonrpc", "2.0");
      jb.add_raw_json ("id", id_str);
      jb.begin_object ();
      jb.add_int ("code", static_cast<int64_t> (error_code::invalid_params));
      jb.add_string ("message", "Missing params");
      jb.end_object ();
      jb.end_object ();
      return jb.str ();
    }

    get_task_request req;
    auto id_result = params.value ()["id"].get_string ();
    if (id_result.error ()) {
      json_builder jb;
      jb.begin_object ();
      jb.add_string ("jsonrpc", "2.0");
      jb.add_raw_json ("id", id_str);
      jb.begin_object ();
      jb.add_int ("code", static_cast<int64_t> (error_code::invalid_params));
      jb.add_string ("message", "Missing id in params");
      jb.end_object ();
      jb.end_object ();
      return jb.str ();
    }
    req.id = std::string (id_result.value ());

    auto result = m_handler->on_get_task (req);
    if (!result) {
      json_builder jb;
      jb.begin_object ();
      jb.add_string ("jsonrpc", "2.0");
      jb.add_raw_json ("id", id_str);
      jb.begin_object ();
      jb.add_int ("code", static_cast<int64_t> (result.error ().code));
      jb.add_string ("message", result.error ().message);
      jb.end_object ();
      jb.end_object ();
      return jb.str ();
    }

    json_builder jb;
    jb.begin_object ();
    jb.add_string ("jsonrpc", "2.0");
    jb.add_raw_json ("id", id_str);
    jb.begin_object ();
    jb.add_raw_json ("result", to_json (*result));
    jb.end_object ();
    jb.end_object ();
    return jb.str ();
  }

  if (method == "ListTasks") {
    list_tasks_request req;
    if (!params.error ()) {
      auto parsed_req = list_tasks_request_from_json (params.value ());
      if (parsed_req) {
        req = *parsed_req;
      }
    }

    auto result = m_handler->on_list_tasks (req);
    if (!result) {
      json_builder jb;
      jb.begin_object ();
      jb.add_string ("jsonrpc", "2.0");
      jb.add_raw_json ("id", id_str);
      jb.begin_object ();
      jb.add_int ("code", static_cast<int64_t> (result.error ().code));
      jb.add_string ("message", result.error ().message);
      jb.end_object ();
      jb.end_object ();
      return jb.str ();
    }

    json_builder jb;
    jb.begin_object ();
    jb.add_string ("jsonrpc", "2.0");
    jb.add_raw_json ("id", id_str);
    jb.begin_object ();
    jb.add_raw_json ("result", to_json (*result));
    jb.end_object ();
    jb.end_object ();
    return jb.str ();
  }

  if (method == "CancelTask") {
    if (params.error ()) {
      json_builder jb;
      jb.begin_object ();
      jb.add_string ("jsonrpc", "2.0");
      jb.add_raw_json ("id", id_str);
      jb.begin_object ();
      jb.add_int ("code", static_cast<int64_t> (error_code::invalid_params));
      jb.add_string ("message", "Missing params");
      jb.end_object ();
      jb.end_object ();
      return jb.str ();
    }

    auto parsed_req = cancel_task_request_from_json (params.value ());
    if (!parsed_req) {
      json_builder jb;
      jb.begin_object ();
      jb.add_string ("jsonrpc", "2.0");
      jb.add_raw_json ("id", id_str);
      jb.begin_object ();
      jb.add_int ("code", static_cast<int64_t> (parsed_req.error ().code));
      jb.add_string ("message", parsed_req.error ().message);
      jb.end_object ();
      jb.end_object ();
      return jb.str ();
    }

    auto result = m_handler->on_cancel_task (*parsed_req);
    if (!result) {
      json_builder jb;
      jb.begin_object ();
      jb.add_string ("jsonrpc", "2.0");
      jb.add_raw_json ("id", id_str);
      jb.begin_object ();
      jb.add_int ("code", static_cast<int64_t> (result.error ().code));
      jb.add_string ("message", result.error ().message);
      jb.end_object ();
      jb.end_object ();
      return jb.str ();
    }

    json_builder jb;
    jb.begin_object ();
    jb.add_string ("jsonrpc", "2.0");
    jb.add_raw_json ("id", id_str);
    jb.begin_object ();
    jb.add_raw_json ("result", to_json (*result));
    jb.end_object ();
    jb.end_object ();
    return jb.str ();
  }

  // Method not found
  json_builder jb;
  jb.begin_object ();
  jb.add_string ("jsonrpc", "2.0");
  jb.add_raw_json ("id", id_str);
  jb.begin_object ();
  jb.add_int ("code", static_cast<int64_t> (error_code::method_not_found));
  jb.add_string ("message", fmt::format ("Method not found: {}", method));
  jb.end_object ();
  jb.end_object ();
  return jb.str ();
}

void
json_rpc_server_transport::serve (uint16_t port)
{
  m_running.store (true);
  const bool started = http::run_http_listener (
      m_running, port, "[a2a-rpc]",
      [this] (const std::string &method, const std::string &path,
              const std::string &body) -> std::string {
        // Discovery endpoint is part of every A2A server.
        if (path == "/.well-known/agent-card.json") {
          return to_json (m_card);
        }
        if (method == "POST") {
          return dispatch_rpc (body);
        }
        json_builder jb;
        jb.begin_object ();
        jb.add_int ("code",
                    static_cast<int64_t> (error_code::method_not_found));
        jb.add_string ("message", "Endpoint not found");
        jb.end_object ();
        return jb.str ();
      });
  m_running.store (false);
  if (started) {
    spdlog::info ("[a2a] JSON-RPC server stopped");
  }
}

void
json_rpc_server_transport::stop ()
{
  m_running.store (false);
}

bool
json_rpc_server_transport::is_running () const
{
  return m_running.load ();
}

} // namespace agentsdk::a2a
