// Part of agentsdk_core_tests; the doctest main lives in
// test_main.cpp.
#include "doctest.h"

#include <agentsdk/mcp/mcp_client.hpp>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

using namespace agentsdk;
using namespace agentsdk::mcp;

namespace
{

struct fake_mcp_fixture
{
  mcp_client client;
  std::filesystem::path log_path;

  fake_mcp_fixture ()
  {
    log_path
        = std::filesystem::temp_directory_path () / "agentsdk_fake_mcp_log";
    std::filesystem::remove (log_path);
    setenv ("FAKE_MCP_LOG", log_path.c_str (), 1);

    REQUIRE (client.launch_server (FAKE_MCP_PATH, {}));

    implementation_info info;
    info.name = "agentsdk-tests";
    info.version = "0.0.0";

    client_capabilities caps;
    caps.roots = roots_capability{ true };
    caps.sampling = true;
    caps.elicitation = true;

    auto init = client.initialize (info, caps);
    REQUIRE (init.has_value ());
    CHECK (init.value ().protocol_version == "2025-06-18");
    CHECK (init.value ().server_info.name == "fake-mcp-server");
    REQUIRE (init.value ().capabilities.tools.has_value ());
    REQUIRE (init.value ().capabilities.resources.has_value ());
    REQUIRE (init.value ().capabilities.prompts.has_value ());
    CHECK (init.value ().capabilities.logging);
    CHECK (init.value ().capabilities.completions);
    CHECK (client.is_initialized ());
  }

  ~fake_mcp_fixture ()
  {
    client.terminate ();
    std::error_code ec;
    std::filesystem::remove (log_path, ec);
  }

  std::string
  read_log () const
  {
    std::ifstream log (log_path);
    REQUIRE (log.good ());
    return std::string (std::istreambuf_iterator<char> (log),
                        std::istreambuf_iterator<char> ());
  }
};

const text_content *
as_text (const content_block &block)
{
  return std::get_if<text_content> (&block);
}

} // namespace

TEST_CASE ("mcp: tools list follows pagination cursors")
{
  fake_mcp_fixture fx;

  auto tools = fx.client.list_all_tools ();
  REQUIRE (tools.has_value ());
  CHECK (tools.value ().size () == 5);

  std::vector<std::string> names;
  for (const tool &t : tools.value ()) {
    names.push_back (t.name);
  }
  CHECK (names
         == std::vector<std::string>{ "echo", "fail", "ask_user", "sample",
                                      "show_roots" });
}

TEST_CASE ("mcp: echo tool round-trips arguments with escaping")
{
  fake_mcp_fixture fx;

  auto r = fx.client.call_tool ("echo", R"({"text":"say \"hi\"\nbye"})");
  REQUIRE (r.has_value ());
  CHECK (!r.value ().is_error);
  REQUIRE (r.value ().content.size () == 1);
  const text_content *text = as_text (r.value ().content[0]);
  REQUIRE (text != nullptr);
  CHECK (text->text == "echo:say \"hi\"\nbye");
}

TEST_CASE ("mcp: tool execution error surfaces as isError result")
{
  fake_mcp_fixture fx;

  auto r = fx.client.call_tool ("fail");
  REQUIRE (r.has_value ());
  CHECK (r.value ().is_error);
  REQUIRE (!r.value ().content.empty ());
  CHECK (as_text (r.value ().content[0])->text == "it broke");
}

TEST_CASE ("mcp: unknown tool maps to a protocol error")
{
  fake_mcp_fixture fx;

  auto r = fx.client.call_tool ("nope");
  REQUIRE (!r.has_value ());
  CHECK (r.error ().code == -32602);
}

TEST_CASE ("mcp: resources list, templates and read")
{
  fake_mcp_fixture fx;

  auto resources = fx.client.list_all_resources ();
  REQUIRE (resources.has_value ());
  REQUIRE (resources.value ().size () == 1);
  CHECK (resources.value ()[0].uri == "file:///notes.txt");

  auto templates = fx.client.list_resource_templates ();
  REQUIRE (templates.has_value ());
  REQUIRE (templates.value ().templates.size () == 1);
  CHECK (templates.value ().templates[0].uri_template == "file:///{path}");

  auto read = fx.client.read_resource ("file:///notes.txt");
  REQUIRE (read.has_value ());
  REQUIRE (read.value ().contents.size () == 1);
  REQUIRE (read.value ().contents[0].text.has_value ());
  CHECK (read.value ().contents[0].text.value () == "hello resource");

  auto missing = fx.client.read_resource ("file:///missing.txt");
  REQUIRE (!missing.has_value ());
  CHECK (missing.error ().code == -32002);

  CHECK (fx.client.subscribe_resource ("file:///notes.txt").has_value ());
  CHECK (fx.client.unsubscribe_resource ("file:///notes.txt").has_value ());
}

TEST_CASE ("mcp: prompts list and get with arguments")
{
  fake_mcp_fixture fx;

  auto prompts = fx.client.list_all_prompts ();
  REQUIRE (prompts.has_value ());
  REQUIRE (prompts.value ().size () == 1);
  CHECK (prompts.value ()[0].name == "greet");

  auto got = fx.client.get_prompt ("greet", R"({"name":"Ada"})");
  REQUIRE (got.has_value ());
  REQUIRE (got.value ().messages.size () == 1);
  CHECK (got.value ().messages[0].role == "user");
  const text_content *text = as_text (got.value ().messages[0].content);
  REQUIRE (text != nullptr);
  CHECK (text->text == "Hello, Ada!");

  auto missing = fx.client.get_prompt ("nope");
  REQUIRE (!missing.has_value ());
  CHECK (missing.error ().code == -32602);
}

TEST_CASE ("mcp: completion filters by prefix")
{
  fake_mcp_fixture fx;

  completion_params params;
  params.ref.type = "ref/prompt";
  params.ref.name = "greet";
  params.argument_name = "language";
  params.argument_value = "pyt";

  auto r = fx.client.complete (params);
  REQUIRE (r.has_value ());
  CHECK (r.value ().values == std::vector<std::string>{ "python", "pytorch" });
  CHECK (!r.value ().has_more);
}

TEST_CASE ("mcp: ping and log level with log notification")
{
  fake_mcp_fixture fx;

  CHECK (fx.client.ping ().has_value ());
  CHECK (fx.client.set_log_level (log_level::warning).has_value ());

  bool saw_log = false;
  fx.client.set_log_handler ([&] (const log_message &msg) {
    if (msg.logger.value_or ("") == "fake") {
      saw_log = true;
      CHECK (msg.level == log_level::warning);
    }
  });

  auto r = fx.client.call_tool ("echo", R"({"text":"x"})");
  REQUIRE (r.has_value ());
  CHECK (saw_log);

  // The fake server logs every request; setLevel must be visible there.
  CHECK (fx.read_log ().find ("logging/setLevel") != std::string::npos);
}

TEST_CASE ("mcp: server elicitation request is answered by the client")
{
  fake_mcp_fixture fx;

  fx.client.set_elicitation_handler (
      [] (const elicitation_params &params)
          -> result<elicitation_result, mcp_error> {
        CHECK (params.message == "Your name?");
        elicitation_result r;
        r.action = "accept";
        r.content = R"({"name":"Ada"})";
        return r;
      });

  auto r = fx.client.call_tool ("ask_user");
  REQUIRE (r.has_value ());
  CHECK (!r.value ().is_error);

  const std::string log = fx.read_log ();
  CHECK (log.find ("elicitation_reply:") != std::string::npos);
  CHECK (log.find ("Ada") != std::string::npos);
}

TEST_CASE ("mcp: server sampling request is answered by the client")
{
  fake_mcp_fixture fx;

  fx.client.set_sampling_handler (
      [] (const sampling_params &params) -> result<sampling_result, mcp_error> {
        REQUIRE (!params.messages.empty ());
        sampling_result r;
        r.role = "assistant";
        text_content text;
        text.text = "Paris";
        r.content = text;
        r.model = "test-model";
        r.stop_reason = "endTurn";
        return r;
      });

  auto r = fx.client.call_tool ("sample");
  REQUIRE (r.has_value ());
  REQUIRE (!r.value ().content.empty ());
  CHECK (as_text (r.value ().content[0])->text == "sampled:Paris");
}

TEST_CASE ("mcp: server roots request is answered from the client list")
{
  fake_mcp_fixture fx;

  root workspace;
  workspace.uri = "file:///workspace";
  workspace.name = "Workspace";
  fx.client.set_roots ({ workspace });

  auto r = fx.client.call_tool ("show_roots");
  REQUIRE (r.has_value ());

  const std::string log = fx.read_log ();
  CHECK (log.find ("roots_reply:") != std::string::npos);
  CHECK (log.find ("file:///workspace") != std::string::npos);
}

TEST_CASE ("mcp: unhandled sampling request fails cleanly")
{
  fake_mcp_fixture fx;

  // No sampling handler installed: the client must answer with a
  // method-not-found error, which the fake server turns into a
  // tool-level failure... in this fake it surfaces as a protocol
  // error path producing an empty sampled text.
  auto r = fx.client.call_tool ("sample");
  REQUIRE (r.has_value ());
  // The fake server got an error reply so sampled text is unknown.
  CHECK (as_text (r.value ().content[0])->text == "sampled:?");
}
