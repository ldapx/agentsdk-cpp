#include <agentsdk/acp/acp_types.hpp>

namespace agentsdk::acp
{

const char *
tool_kind_name (tool_kind kind)
{
  switch (kind) {
  case tool_kind::read:
    return "read";
  case tool_kind::edit:
    return "edit";
  case tool_kind::del:
    return "delete";
  case tool_kind::move:
    return "move";
  case tool_kind::search:
    return "search";
  case tool_kind::execute:
    return "execute";
  case tool_kind::think:
    return "think";
  case tool_kind::fetch:
    return "fetch";
  case tool_kind::switch_mode:
    return "switch_mode";
  case tool_kind::other:
    return "other";
  }
  return "other";
}

const char *
tool_call_status_name (tool_call_status status)
{
  switch (status) {
  case tool_call_status::pending:
    return "pending";
  case tool_call_status::in_progress:
    return "in_progress";
  case tool_call_status::completed:
    return "completed";
  case tool_call_status::failed:
    return "failed";
  }
  return "pending";
}

const char *
permission_option_kind_name (permission_option_kind kind)
{
  switch (kind) {
  case permission_option_kind::allow_once:
    return "allow_once";
  case permission_option_kind::allow_always:
    return "allow_always";
  case permission_option_kind::reject_once:
    return "reject_once";
  case permission_option_kind::reject_always:
    return "reject_always";
  }
  return "allow_once";
}

std::optional<tool_kind>
parse_tool_kind (const std::string &str)
{
  if (str == "read")
    return tool_kind::read;
  if (str == "edit")
    return tool_kind::edit;
  if (str == "delete")
    return tool_kind::del;
  if (str == "move")
    return tool_kind::move;
  if (str == "search")
    return tool_kind::search;
  if (str == "execute")
    return tool_kind::execute;
  if (str == "think")
    return tool_kind::think;
  if (str == "fetch")
    return tool_kind::fetch;
  if (str == "switch_mode")
    return tool_kind::switch_mode;
  if (str == "other")
    return tool_kind::other;
  return std::nullopt;
}

std::optional<tool_call_status>
parse_tool_call_status (const std::string &str)
{
  if (str == "pending")
    return tool_call_status::pending;
  if (str == "in_progress")
    return tool_call_status::in_progress;
  if (str == "completed")
    return tool_call_status::completed;
  if (str == "failed")
    return tool_call_status::failed;
  return std::nullopt;
}

config_option_category
parse_config_option_category (const std::string &str)
{
  if (str == "mode")
    return config_option_category::mode;
  if (str == "model")
    return config_option_category::model;
  if (str == "model_config")
    return config_option_category::model_config;
  if (str == "thought_level")
    return config_option_category::thought_level;
  return config_option_category::custom;
}

config_option_type
parse_config_option_type (const std::string &str)
{
  if (str == "boolean")
    return config_option_type::boolean;
  return config_option_type::select;
}

} // namespace agentsdk::acp
