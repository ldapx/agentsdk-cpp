#pragma once

#include <string>
#include <vector>

namespace agentsdk::acp
{

class acp_client;

/**
 * Information about a discovered ACP-compatible agent.
 */
struct agent_entry
{
  /** Internal name (e.g. "opencode", "cline", "goose"). */
  std::string name;

  /** Human-readable display name (e.g. "OpenCode", "Cline"). */
  std::string display_name;

  /** Path to the agent executable. */
  std::string command;

  /** Default command-line arguments. */
  std::vector<std::string> args;
};

/**
 * Discovers and launches ACP-compatible agents.
 *
 * Scans PATH and common install locations for known ACP agents,
 * and provides a unified interface to launch them.
 */
class acp_agent_manager
{
public:
  /**
   * Scan for available ACP agents.
   *
   * Searches PATH and common install locations for known agents.
   *
   * :return: List of discovered agents.
   */
  std::vector<agent_entry> discover_agents ();

  /**
   * Launch an agent and connect the ACP client.
   *
   * :param client: The ACP client to connect.
   * :param agent: The agent to launch.
   * :return: true on success.
   */
  bool launch (acp_client &client, const agent_entry &agent);

  /**
   * Get the default agent entry for a given name.
   *
   * :param name: Agent name (e.g. "opencode").
   * :return: The agent entry, or a default with the name as command.
   */
  agent_entry make_entry (const std::string &name) const;

private:
  bool find_in_path (const std::string &name, std::string &out_path);
  bool is_executable (const std::string &path);
};

} // namespace agentsdk::acp
