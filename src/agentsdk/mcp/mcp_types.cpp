#include <agentsdk/mcp/mcp_types.hpp>

namespace agentsdk::mcp
{

const char *
log_level_name (log_level level)
{
  switch (level) {
  case log_level::debug:
    return "debug";
  case log_level::info:
    return "info";
  case log_level::notice:
    return "notice";
  case log_level::warning:
    return "warning";
  case log_level::error:
    return "error";
  case log_level::critical:
    return "critical";
  case log_level::alert:
    return "alert";
  case log_level::emergency:
    return "emergency";
  }
  return "info";
}

std::optional<log_level>
parse_log_level (const std::string &str)
{
  if (str == "debug")
    return log_level::debug;
  if (str == "info")
    return log_level::info;
  if (str == "notice")
    return log_level::notice;
  if (str == "warning")
    return log_level::warning;
  if (str == "error")
    return log_level::error;
  if (str == "critical")
    return log_level::critical;
  if (str == "alert")
    return log_level::alert;
  if (str == "emergency")
    return log_level::emergency;
  return std::nullopt;
}

namespace
{

int
log_level_rank (log_level level)
{
  switch (level) {
  case log_level::debug:
    return 0;
  case log_level::info:
    return 1;
  case log_level::notice:
    return 2;
  case log_level::warning:
    return 3;
  case log_level::error:
    return 4;
  case log_level::critical:
    return 5;
  case log_level::alert:
    return 6;
  case log_level::emergency:
    return 7;
  }
  return 1;
}

} // namespace

bool
log_level_at_least (log_level a, log_level b)
{
  return log_level_rank (a) >= log_level_rank (b);
}

} // namespace agentsdk::mcp
