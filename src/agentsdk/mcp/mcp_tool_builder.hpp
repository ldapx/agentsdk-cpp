#pragma once

#include <agentsdk/common/json_builder.hpp>
#include <agentsdk/mcp/mcp_types.hpp>

#include <string>
#include <utility>
#include <vector>

namespace agentsdk::mcp
{

/**
 * Fluent builder for MCP tool definitions.
 *
 * Convenience replacement for hand-writing ``inputSchema`` JSON strings.
 * Mirrors the ``tool_builder`` API previously provided by ``cpp-mcp`` so
 * server code can declare typed parameters without manual JSON escaping:
 *
 * ```cpp
 * tool t = tool_builder ("describe_command")
 *     .with_description ("Get detailed usage for a command.")
 *     .with_string_param ("name", "Name of the command.")
 *     .build ();
 * ```
 *
 * Schemas are emitted as ``{"type":"object","properties":{...}}`` with an
 * optional ``"required"`` array.  ``with_*_param`` defaults to required;
 * pass ``false`` for optional parameters.
 */
class tool_builder
{
public:
  explicit tool_builder (std::string name)
  {
    m_tool.name = std::move (name);
    m_tool.input_schema = "{}";
  }

  tool_builder &
  with_description (std::string description)
  {
    m_tool.description = std::move (description);
    return *this;
  }

  tool_builder &
  with_title (std::string title)
  {
    m_tool.title = std::move (title);
    return *this;
  }

  tool_builder &
  with_string_param (const std::string &name, const std::string &description,
                     bool required = true)
  {
    add_param (name, "string", description, required);
    return *this;
  }

  tool_builder &
  with_boolean_param (const std::string &name, const std::string &description,
                      bool required = true)
  {
    add_param (name, "boolean", description, required);
    return *this;
  }

  tool_builder &
  with_number_param (const std::string &name, const std::string &description,
                     bool required = true)
  {
    add_param (name, "number", description, required);
    return *this;
  }

  tool_builder &
  with_integer_param (const std::string &name, const std::string &description,
                      bool required = true)
  {
    add_param (name, "integer", description, required);
    return *this;
  }

  tool
  build () const
  {
    tool out = m_tool;
    out.input_schema = build_schema ();
    return out;
  }

private:
  struct param
  {
    std::string name;
    std::string type;
    std::string description;
    bool required = true;
  };

  void
  add_param (const std::string &name, const std::string &type,
             const std::string &description, bool required)
  {
    m_params.push_back ({ name, type, description, required });
  }

  std::string
  build_schema () const
  {
    if (m_params.empty ()) {
      return R"({"type":"object","properties":{}})";
    }
    json_builder jb;
    jb.begin_object ();
    jb.add_string ("type", "object");
    jb.begin_object ("properties");
    for (const param &p : m_params) {
      jb.begin_object (p.name);
      jb.add_string ("type", p.type);
      if (!p.description.empty ()) {
        jb.add_string ("description", p.description);
      }
      jb.end_object ();
    }
    jb.end_object ();
    bool any_required = false;
    for (const param &p : m_params) {
      if (p.required) {
        any_required = true;
        break;
      }
    }
    if (any_required) {
      jb.begin_array ("required");
      for (const param &p : m_params) {
        if (p.required) {
          jb.add_array_string (p.name);
        }
      }
      jb.end_array ();
    }
    jb.end_object ();
    return jb.str ();
  }

  tool m_tool;
  std::vector<param> m_params;
};

} // namespace agentsdk::mcp
