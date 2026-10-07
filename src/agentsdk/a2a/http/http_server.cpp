#include <agentsdk/a2a/http/http_server.hpp>

#include <spdlog/spdlog.h>

#include <agentsdk/a2a/http/http_listener.hpp>
#include <agentsdk/a2a/json_util.hpp>

namespace agentsdk::a2a
{

http_json_server_transport::http_json_server_transport (
    agent_card card, std::shared_ptr<request_handler> handler)
    : m_card (std::move (card)), m_handler (std::move (handler))
{
}

http_json_server_transport::~http_json_server_transport () { stop (); }

std::string
http_json_server_transport::dispatch (const std::string &method,
                                      const std::string &path,
                                      const std::string &body)
{
  (void)method;

  // Agent card discovery endpoint.
  if (path == "/.well-known/agent-card.json") {
    return to_json (m_card);
  }

  // POST /message:send
  if (path.find ("/message:send") != std::string::npos) {
    auto parsed = parse_json (body);
    if (!parsed) {
      json_builder jb;
      jb.begin_object ();
      jb.add_int ("code", static_cast<int64_t> (parsed.error ().code));
      jb.add_string ("message", parsed.error ().message);
      jb.end_object ();
      return jb.str ();
    }

    auto req = send_message_request_from_json (parsed.value ());
    if (!req) {
      json_builder jb;
      jb.begin_object ();
      jb.add_int ("code", static_cast<int64_t> (req.error ().code));
      jb.add_string ("message", req.error ().message);
      jb.end_object ();
      return jb.str ();
    }

    auto result = m_handler->on_send_message (*req);
    if (!result) {
      json_builder jb;
      jb.begin_object ();
      jb.add_int ("code", static_cast<int64_t> (result.error ().code));
      jb.add_string ("message", result.error ().message);
      jb.end_object ();
      return jb.str ();
    }

    // Wrap in a send_message_response envelope.
    if (auto *t = std::get_if<task> (&*result)) {
      json_builder jb;
      jb.begin_object ();
      jb.add_raw_json ("task", to_json (*t));
      jb.end_object ();
      return jb.str ();
    }
    if (auto *m = std::get_if<message> (&*result)) {
      json_builder jb;
      jb.begin_object ();
      jb.add_raw_json ("message", to_json (*m));
      jb.end_object ();
      return jb.str ();
    }
  }

  // GET /tasks/{id}
  if (path.find ("/tasks/") != std::string::npos
      && path.find (":cancel") == std::string::npos) {
    // Extract task ID from path.
    std::string tasks_prefix = "/tasks/";
    if (m_card.supported_interfaces.size () > 0
        && path.find (m_card.supported_interfaces[0].url)
               != std::string::npos) {
      // Handle absolute URLs
    }
    auto pos = path.rfind (tasks_prefix);
    if (pos != std::string::npos) {
      std::string task_id = path.substr (pos + tasks_prefix.size ());
      // Remove query string
      auto qpos = task_id.find ('?');
      if (qpos != std::string::npos) {
        task_id = task_id.substr (0, qpos);
      }

      get_task_request req;
      req.id = task_id;
      auto result = m_handler->on_get_task (req);
      if (!result) {
        json_builder jb;
        jb.begin_object ();
        jb.add_int ("code", static_cast<int64_t> (result.error ().code));
        jb.add_string ("message", result.error ().message);
        jb.end_object ();
        return jb.str ();
      }
      return to_json (*result);
    }
  }

  // POST /tasks/{id}:cancel
  if (path.find (":cancel") != std::string::npos) {
    std::string tasks_prefix = "/tasks/";
    auto pos = path.rfind (tasks_prefix);
    if (pos != std::string::npos) {
      std::string remainder = path.substr (pos + tasks_prefix.size ());
      auto colon_pos = remainder.find (':');
      std::string task_id = remainder.substr (0, colon_pos);

      cancel_task_request req;
      req.id = task_id;
      auto result = m_handler->on_cancel_task (req);
      if (!result) {
        json_builder jb;
        jb.begin_object ();
        jb.add_int ("code", static_cast<int64_t> (result.error ().code));
        jb.add_string ("message", result.error ().message);
        jb.end_object ();
        return jb.str ();
      }
      return to_json (*result);
    }
  }

  // 404
  json_builder jb;
  jb.begin_object ();
  jb.add_int ("code", static_cast<int64_t> (error_code::method_not_found));
  jb.add_string ("message", "Endpoint not found");
  jb.end_object ();
  return jb.str ();
}

void
http_json_server_transport::serve (uint16_t port)
{
  m_running.store (true);
  const bool started = http::run_http_listener (
      m_running, port, "[a2a]",
      [this] (const std::string &method, const std::string &path,
              const std::string &body) -> std::string {
        return dispatch (method, path, body);
      });
  m_running.store (false);
  if (started) {
    spdlog::info ("[a2a] HTTP server stopped");
  }
}

void
http_json_server_transport::stop ()
{
  m_running.store (false);
}

bool
http_json_server_transport::is_running () const
{
  return m_running.load ();
}

} // namespace agentsdk::a2a
