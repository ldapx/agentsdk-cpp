#include <agentsdk/a2a/http/http_client.hpp>
#include <agentsdk/a2a/result.hpp>

#include <spdlog/spdlog.h>

#include <agentsdk/a2a/json_util.hpp>

namespace agentsdk::a2a
{

namespace
{

static size_t
write_callback (char *ptr, size_t size, size_t nmemb, void *userdata)
{
  auto *buffer = static_cast<std::string *> (userdata);
  buffer->append (ptr, size * nmemb);
  return size * nmemb;
}

} // namespace

http_json_client_transport::http_json_client_transport (
    const agent_interface &iface)
    : m_base_url (iface.url), m_tenant (iface.tenant.value_or (""))
{
}

http_json_client_transport::~http_json_client_transport () = default;

result<std::string, a2a_error>
http_json_client_transport::post (const std::string &path,
                                  const std::string &body)
{
  CURL *easy = curl_easy_init ();
  if (!easy) {
    return a2a_error{ error_code::internal_error,
                      "Failed to initialize libcurl",
                      {} };
  }

  std::string url = m_base_url + path;
  std::string response_body;

  curl_easy_setopt (easy, CURLOPT_URL, url.c_str ());
  curl_easy_setopt (easy, CURLOPT_POST, 1L);
  curl_easy_setopt (easy, CURLOPT_POSTFIELDS, body.c_str ());
  curl_easy_setopt (easy, CURLOPT_POSTFIELDSIZE,
                    static_cast<long> (body.size ()));
  curl_easy_setopt (easy, CURLOPT_WRITEFUNCTION, write_callback);
  curl_easy_setopt (easy, CURLOPT_WRITEDATA, &response_body);

  struct curl_slist *headers = nullptr;
  headers = curl_slist_append (headers, "Content-Type: application/json");
  headers = curl_slist_append (headers, "A2A-Version: 1.0");
  curl_easy_setopt (easy, CURLOPT_HTTPHEADER, headers);

  CURLcode res = curl_easy_perform (easy);
  long http_code = 0;
  curl_easy_getinfo (easy, CURLINFO_RESPONSE_CODE, &http_code);

  curl_slist_free_all (headers);
  curl_easy_cleanup (easy);

  if (res != CURLE_OK) {
    return a2a_error{ error_code::internal_error,
                      fmt::format ("HTTP request failed: {}",
                                   curl_easy_strerror (res)),
                      {} };
  }

  if (http_code < 200 || http_code >= 300) {
    return a2a_error{ error_code::internal_error,
                      fmt::format ("HTTP {}", http_code),
                      {} };
  }

  return response_body;
}

result<std::string, a2a_error>
http_json_client_transport::get (const std::string &path)
{
  CURL *easy = curl_easy_init ();
  if (!easy) {
    return a2a_error{ error_code::internal_error,
                      "Failed to initialize libcurl",
                      {} };
  }

  std::string url = m_base_url + path;
  std::string response_body;

  curl_easy_setopt (easy, CURLOPT_URL, url.c_str ());
  curl_easy_setopt (easy, CURLOPT_WRITEFUNCTION, write_callback);
  curl_easy_setopt (easy, CURLOPT_WRITEDATA, &response_body);

  struct curl_slist *headers = nullptr;
  headers = curl_slist_append (headers, "Accept: application/json");
  headers = curl_slist_append (headers, "A2A-Version: 1.0");
  curl_easy_setopt (easy, CURLOPT_HTTPHEADER, headers);

  CURLcode res = curl_easy_perform (easy);
  long http_code = 0;
  curl_easy_getinfo (easy, CURLINFO_RESPONSE_CODE, &http_code);

  curl_slist_free_all (headers);
  curl_easy_cleanup (easy);

  if (res != CURLE_OK) {
    return a2a_error{ error_code::internal_error,
                      fmt::format ("HTTP request failed: {}",
                                   curl_easy_strerror (res)),
                      {} };
  }

  if (http_code < 200 || http_code >= 300) {
    return a2a_error{ error_code::internal_error,
                      fmt::format ("HTTP {}", http_code),
                      {} };
  }

  return response_body;
}

result<send_message_response, a2a_error>
http_json_client_transport::send_message (const send_message_request &request)
{
  std::string path = "/message:send";
  if (!m_tenant.empty ()) {
    path = "/" + m_tenant + path;
  }

  auto body = to_json (request);
  auto resp = post (path, body);
  if (!resp) {
    return resp.error ();
  }

  auto parsed = parse_json (*resp);
  if (!parsed) {
    return parsed.error ();
  }

  auto element = parsed.value ();
  auto task_el = element["task"];
  if (!task_el.error ()) {
    auto t = task_from_json (task_el.value ());
    if (t) {
      return send_message_response{ std::move (*t) };
    }
  }

  auto msg_el = element["message"];
  if (!msg_el.error ()) {
    auto m = message_from_json (msg_el.value ());
    if (m) {
      return send_message_response{ std::move (*m) };
    }
  }

  return a2a_error{ error_code::invalid_agent_response,
                    "Response contains neither task nor message",
                    {} };
}

result<task, a2a_error>
http_json_client_transport::get_task (const get_task_request &request)
{
  std::string path = "/tasks/" + request.id;
  if (!m_tenant.empty ()) {
    path = "/" + m_tenant + path;
  }
  if (request.history_length) {
    path += "?historyLength=" + std::to_string (*request.history_length);
  }

  auto resp = get (path);
  if (!resp) {
    return resp.error ();
  }

  auto parsed = parse_json (*resp);
  if (!parsed) {
    return parsed.error ();
  }

  return task_from_json (parsed.value ());
}

result<list_tasks_response, a2a_error>
http_json_client_transport::list_tasks (const list_tasks_request &request)
{
  (void)request;
  std::string path = "/tasks";
  if (!m_tenant.empty ()) {
    path = "/" + m_tenant + path;
  }

  auto resp = get (path);
  if (!resp) {
    return resp.error ();
  }

  auto parsed = parse_json (*resp);
  if (!parsed) {
    return parsed.error ();
  }

  return list_tasks_response_from_json (parsed.value ());
}

result<task, a2a_error>
http_json_client_transport::cancel_task (const cancel_task_request &request)
{
  std::string path = "/tasks/" + request.id + ":cancel";
  if (!m_tenant.empty ()) {
    path = "/" + m_tenant + path;
  }

  auto resp = post (path, "{}");
  if (!resp) {
    return resp.error ();
  }

  auto parsed = parse_json (*resp);
  if (!parsed) {
    return parsed.error ();
  }

  return task_from_json (parsed.value ());
}

std::unique_ptr<stream_handle>
http_json_client_transport::send_streaming_message (
    const send_message_request &request, stream_observer &observer)
{
  (void)request;
  (void)observer;
  // TODO: implement SSE streaming via curl_multi
  return nullptr;
}

std::unique_ptr<stream_handle>
http_json_client_transport::subscribe_task (const std::string &task_id,
                                            stream_observer &observer)
{
  (void)task_id;
  (void)observer;
  // TODO: implement SSE streaming via curl_multi
  return nullptr;
}

} // namespace agentsdk::a2a
