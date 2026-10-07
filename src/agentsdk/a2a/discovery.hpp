#pragma once

#include <chrono>
#include <agentsdk/a2a/result.hpp>
#include <mutex>
#include <string>
#include <unordered_map>

#include <agentsdk/a2a/agent_card.hpp>
#include <agentsdk/a2a/errors.hpp>

namespace agentsdk::a2a
{

/**
 * Fetches agent cards from the well-known discovery endpoint.
 *
 * Example::
 *
 *   auto card = agent_card_fetcher::fetch ("http://localhost:8080");
 *   if (card) {
 *     std::cout << card->name << "\n";
 *   }
 */
class agent_card_fetcher
{
public:
  /**
   * Fetches the agent card from the well-known endpoint.
   *
   * Sends a GET request to ``{base_url}/.well-known/agent-card.json``
   * and parses the response.
   *
   * :param base_url: The agent's base URL.
   * :return: The agent card, or an error.
   */
  static result<agent_card, a2a_error>
  fetch (const std::string &base_url);

  /**
   * Fetches with an in-memory TTL cache.
   *
   * Subsequent calls within the TTL window return the cached card
   * without making a network request.
   *
   * :param base_url: The agent's base URL.
   * :param ttl: Cache time-to-live (default 5 minutes).
   * :return: The agent card, or an error.
   */
  static result<agent_card, a2a_error>
  fetch_cached (const std::string &base_url,
                std::chrono::seconds ttl = std::chrono::seconds{ 300 });

  /** Clear the entire cache. */
  static void clear_cache ();

  /** Clear the cache entry for a specific base URL. */
  static void clear_cache (const std::string &base_url);

private:
  struct cache_entry
  {
    agent_card card;
    std::chrono::steady_clock::time_point expires_at;
  };

  static std::mutex s_cache_mutex;
  static std::unordered_map<std::string, cache_entry> s_cache;
};

} // namespace agentsdk::a2a
