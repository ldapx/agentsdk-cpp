#include <agentsdk/a2a/discovery.hpp>
#include <agentsdk/a2a/result.hpp>

#include <spdlog/spdlog.h>

#include <agentsdk/a2a/json_util.hpp>

extern "C" {
#include <curl/curl.h>
}

namespace agentsdk::a2a
{

std::mutex agent_card_fetcher::s_cache_mutex;
std::unordered_map<std::string, agent_card_fetcher::cache_entry>
    agent_card_fetcher::s_cache;

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

result<agent_card, a2a_error>
agent_card_fetcher::fetch (const std::string &base_url)
{
  std::string url = base_url + "/.well-known/agent-card.json";

  CURL *easy = curl_easy_init ();
  if (!easy) {
    return a2a_error{ error_code::internal_error,
                      "Failed to initialize libcurl",
                      {} };
  }

  std::string response_body;

  curl_easy_setopt (easy, CURLOPT_URL, url.c_str ());
  curl_easy_setopt (easy, CURLOPT_WRITEFUNCTION, write_callback);
  curl_easy_setopt (easy, CURLOPT_WRITEDATA, &response_body);
  curl_easy_setopt (easy, CURLOPT_TIMEOUT, 10L);

  struct curl_slist *headers = nullptr;
  headers = curl_slist_append (headers, "Accept: application/json");
  curl_easy_setopt (easy, CURLOPT_HTTPHEADER, headers);

  CURLcode res = curl_easy_perform (easy);
  long http_code = 0;
  curl_easy_getinfo (easy, CURLINFO_RESPONSE_CODE, &http_code);

  curl_slist_free_all (headers);
  curl_easy_cleanup (easy);
  if (res != CURLE_OK) {
    return a2a_error{ error_code::internal_error,
                      fmt::format ("Failed to fetch agent card: {}",
                                   curl_easy_strerror (res)),
                      {} };
  }

  if (http_code != 200) {
    return a2a_error{ error_code::internal_error,
                      fmt::format ("HTTP {} fetching agent card", http_code),
                      {} };
  }

  auto parsed = parse_json (response_body);
  if (!parsed) {
    return parsed.error ();
  }

  return agent_card_from_json (parsed.value ());
}

result<agent_card, a2a_error>
agent_card_fetcher::fetch_cached (const std::string &base_url,
                                  std::chrono::seconds ttl)
{
  {
    std::lock_guard<std::mutex> lock (s_cache_mutex);
    auto it = s_cache.find (base_url);
    if (it != s_cache.end ()) {
      if (std::chrono::steady_clock::now () < it->second.expires_at) {
        return it->second.card;
      }
      s_cache.erase (it);
    }
  }

  auto result = fetch (base_url);
  if (result) {
    std::lock_guard<std::mutex> lock (s_cache_mutex);
    cache_entry entry;
    entry.card = *result;
    entry.expires_at = std::chrono::steady_clock::now () + ttl;
    s_cache[base_url] = std::move (entry);
  }

  return result;
}

void
agent_card_fetcher::clear_cache ()
{
  std::lock_guard<std::mutex> lock (s_cache_mutex);
  s_cache.clear ();
}

void
agent_card_fetcher::clear_cache (const std::string &base_url)
{
  std::lock_guard<std::mutex> lock (s_cache_mutex);
  s_cache.erase (base_url);
}

} // namespace agentsdk::a2a
