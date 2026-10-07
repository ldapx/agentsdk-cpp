// Test host embedding the real agentsdk::mcp::mcp_server.
//
// Used by test_mcp_server.cpp: the test drives this process over stdio
// with raw JSON-RPC and asserts on the responses.  This exercises the
// actual server class (routing, version negotiation, error codes) rather
// than a second implementation.

#include <agentsdk/mcp/mcp_server.hpp>

#include <simdjson.h>

#include <iostream>

using namespace agentsdk;
using namespace agentsdk::mcp;

namespace
{

int
int_arg (const std::string &args_json, const std::string &field)
{
  auto parsed = parse_json (args_json);
  if (!parsed) {
    return 0;
  }
  auto v = parsed.value ()[field].get_int64 ();
  if (v.error ()) {
    return 0;
  }
  return static_cast<int> (v.value ());
}

std::string
string_arg (const std::string &args_json, const std::string &field)
{
  auto parsed = parse_json (args_json);
  if (!parsed) {
    return "";
  }
  auto v = parsed.value ()[field].get_string ();
  if (v.error ()) {
    return "";
  }
  return std::string (v.value ());
}

tool_result
text_result (const std::string &text)
{
  tool_result r;
  text_content t;
  t.text = text;
  r.content.push_back (t);
  return r;
}

} // namespace

int
main ()
{
  implementation_info info;
  info.name = "test-host";
  info.version = "1.0";

  server_capabilities caps;
  caps.tools = tools_capability{ true };
  caps.resources = resources_capability{ true, true };
  caps.prompts = prompts_capability{ true };
  caps.logging = true;
  caps.completions = true;

  mcp_server server (info, caps);
  server.set_instructions ("Test instructions.");

  server.set_list_tools_handler ([] (const std::optional<std::string> &)
                                     -> result<tools_list_result, mcp_error> {
    tools_list_result r;
    tool add;
    add.name = "add";
    add.description = "Add two numbers";
    add.input_schema = R"({"type":"object","properties":{)"
                       R"("a":{"type":"integer"},"b":{"type":"integer"}}})";
    r.tools.push_back (add);
    for (const char *name : { "need_name", "need_sample", "need_roots" }) {
      tool t;
      t.name = name;
      t.input_schema = "{}";
      r.tools.push_back (t);
    }
    return r;
  });

  server.set_call_tool_handler ([&] (const std::string &name,
                                     const std::string &args)
                                    -> result<tool_result, mcp_error> {
    if (name == "add") {
      int sum = int_arg (args, "a") + int_arg (args, "b");
      return text_result ("sum:" + std::to_string (sum));
    }
    if (name == "need_name") {
      elicitation_params p;
      p.message = "What is your name?";
      p.requested_schema = R"({"type":"object","properties":{)"
                           R"("name":{"type":"string"}},"required":)"
                           R"(["name"]})";
      auto e = server.request_elicitation (p);
      if (!e) {
        return e.error ();
      }
      if (e.value ().action != "accept" || !e.value ().content) {
        return mcp_error{ INTERNAL_ERROR, "elicitation declined",
                          std::nullopt };
      }
      return text_result ("elicited:" + e.value ().content.value ());
    }
    if (name == "need_sample") {
      sampling_params p;
      sampling_message m;
      m.role = "user";
      text_content t;
      t.text = "Say hi";
      m.content = t;
      p.messages.push_back (m);
      p.max_tokens = 8;
      auto s = server.request_sampling (p);
      if (!s) {
        return s.error ();
      }
      const text_content *text
          = std::get_if<text_content> (&s.value ().content);
      return text_result ("sampled:" + (text ? text->text : std::string ("?")));
    }
    if (name == "need_roots") {
      auto roots = server.request_roots ();
      if (!roots) {
        return roots.error ();
      }
      return text_result ("roots:"
                          + std::to_string (roots.value ().roots.size ()));
    }
    return mcp_error{ METHOD_NOT_FOUND, "Unknown tool: " + name, std::nullopt };
  });

  server.set_list_resources_handler (
      [] (const std::optional<std::string> &)
          -> result<resources_list_result, mcp_error> {
        resources_list_result r;
        resource res;
        res.uri = "test://hello";
        res.name = "hello";
        res.mime_type = "text/plain";
        r.resources.push_back (res);
        return r;
      });

  server.set_list_templates_handler (
      [] (const std::optional<std::string> &)
          -> result<resource_templates_result, mcp_error> {
        return resource_templates_result{};
      });

  server.set_read_resource_handler (
      [] (const std::string &uri) -> result<resource_read_result, mcp_error> {
        if (uri != "test://hello") {
          return mcp_error{ RESOURCE_NOT_FOUND, "Resource not found",
                            std::nullopt };
        }
        resource_read_result r;
        resource_contents c;
        c.uri = uri;
        c.mime_type = "text/plain";
        c.text = "hi";
        r.contents.push_back (c);
        return r;
      });

  server.set_subscribe_handler (
      [] (const std::string &) -> result<void, mcp_error> {
        return result<void, mcp_error>{};
      });
  server.set_unsubscribe_handler (
      [] (const std::string &) -> result<void, mcp_error> {
        return result<void, mcp_error>{};
      });

  server.set_list_prompts_handler (
      [] (const std::optional<std::string> &)
          -> result<prompts_list_result, mcp_error> {
        prompts_list_result r;
        prompt p;
        p.name = "shout";
        prompt_argument a;
        a.name = "text";
        a.required = true;
        p.arguments.push_back (a);
        r.prompts.push_back (p);
        return r;
      });

  server.set_get_prompt_handler (
      [] (const std::string &name, const std::optional<std::string> &args)
          -> result<prompt_result, mcp_error> {
        if (name != "shout") {
          return mcp_error{ INVALID_PARAMS, "Unknown prompt", std::nullopt };
        }
        std::string text = args ? string_arg (*args, "text") : "";
        prompt_result r;
        prompt_message m;
        m.role = "user";
        text_content t;
        t.text = "shout:" + text;
        m.content = t;
        r.messages.push_back (m);
        return r;
      });

  server.set_completion_handler (
      [] (const completion_params &p) -> result<completion_result, mcp_error> {
        (void)p;
        completion_result r;
        r.values = { "alpha", "alpine" };
        return r;
      });

  server.run ();
  return 0;
}
