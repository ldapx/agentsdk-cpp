#include <agentsdk/acp/acp_agent_manager.hpp>
#include <agentsdk/acp/acp_client.hpp>

#include <spdlog/spdlog.h>

#include <cstdlib>
#include <unistd.h>

namespace agentsdk::acp
{

namespace
{

/**
 * Known ACP agents and their binary names.
 */
struct known_agent
{
  const char *name;
  const char *display_name;
  const char *binary;
  const char *args[4]; // null-terminated
};

constexpr known_agent KNOWN_AGENTS[] = {
  { "opencode", "OpenCode", "opencode", { "acp", nullptr } },
  { "cline", "Cline", "cline", { "--stdio", nullptr } },
  { "goose", "Goose", "goose", { nullptr } },
  { "gemini", "Gemini CLI", "gemini", { nullptr } },
  { "copilot", "GitHub Copilot", "copilot", { nullptr } },
  { "codex", "Codex CLI", "codex", { nullptr } },
};

} // namespace

std::vector<agent_entry>
acp_agent_manager::discover_agents ()
{
  std::vector<agent_entry> found;

  for (auto &known : KNOWN_AGENTS) {
    std::string path;
    if (find_in_path (known.binary, path)) {
      agent_entry entry;
      entry.name = known.name;
      entry.display_name = known.display_name;
      entry.command = path;

      // Copy args from null-terminated array
      for (int i = 0; known.args[i] != nullptr; ++i) {
        entry.args.push_back (known.args[i]);
      }

      found.push_back (std::move (entry));

      spdlog::info ("[acp] Found agent: {} at {}", known.display_name, path);
    }
  }

  return found;
}

bool
acp_agent_manager::launch (acp_client &client, const agent_entry &agent)
{
  if (agent.command.empty ()) {
    spdlog::error ("[acp] Cannot launch agent: no command specified");
    return false;
  }

  if (!is_executable (agent.command)) {
    spdlog::error ("[acp] Agent not executable: {}", agent.command);
    return false;
  }

  spdlog::info ("[acp] Launching agent: {} ({})", agent.display_name,
                agent.command);

  // Log the full command for debugging
  std::string cmd_line = agent.command;
  for (const auto &arg : agent.args) {
    cmd_line += " " + arg;
  }

  return client.launch_agent (agent.command, agent.args);
}

agent_entry
acp_agent_manager::make_entry (const std::string &name) const
{
  agent_entry entry;
  entry.name = name;
  entry.display_name = name;
  entry.command = name;
  entry.args = { "--stdio" };
  return entry;
}

bool
acp_agent_manager::find_in_path (const std::string &name, std::string &out_path)
{
  const char *path_env = std::getenv ("PATH");
  if (!path_env)
    return false;

  std::string paths (path_env);
  size_t start = 0;

  while (start < paths.size ()) {
    auto pos = paths.find (':', start);
    std::string dir;
    if (pos == std::string::npos) {
      dir = paths.substr (start);
      start = paths.size ();
    } else {
      dir = paths.substr (start, pos - start);
      start = pos + 1;
    }

    if (dir.empty ())
      dir = ".";

    std::string candidate = dir + "/" + name;
    if (is_executable (candidate)) {
      out_path = candidate;
      return true;
    }
  }

  return false;
}

bool
acp_agent_manager::is_executable (const std::string &path)
{
  return access (path.c_str (), X_OK) == 0;
}

} // namespace agentsdk::acp
