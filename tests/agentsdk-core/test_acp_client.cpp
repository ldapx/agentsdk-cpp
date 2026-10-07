// Part of agentsdk_core_tests; the doctest main lives in
// test_main.cpp.
#include "doctest.h"

#include <agentsdk/acp/acp_client.hpp>
#include <agentsdk/acp/acp_session.hpp>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

using namespace agentsdk::acp;

namespace
{

struct fake_agent_fixture
{
  acp_client client;
  acp_session session{ client };
  std::filesystem::path log_path;

  explicit fake_agent_fixture (const std::string &read_target = "")
  {
    log_path
        = std::filesystem::temp_directory_path () / "agentsdk_fake_agent_log";
    std::filesystem::remove (log_path);

    setenv ("FAKE_AGENT_LOG", log_path.c_str (), 1);
    if (!read_target.empty ()) {
      setenv ("FAKE_AGENT_READ_TARGET", read_target.c_str (), 1);
    } else {
      unsetenv ("FAKE_AGENT_READ_TARGET");
    }

    REQUIRE (client.launch_agent (FAKE_AGENT_PATH, {}));

    client_capabilities caps;
    caps.fs.read_text_file = true;
    caps.fs.write_text_file = false;
    caps.terminal = false;

    implementation_info info;
    info.name = "weasel-tests";
    info.title = "Weasel Tests";
    info.version = "0.0.0";

    CHECK (session.initialize (caps, info));
    CHECK (session.new_session ("/tmp") == "fake-session");
  }

  ~fake_agent_fixture ()
  {
    client.terminate ();

    if (!log_path.empty ()) {
      std::error_code ec;
      std::filesystem::remove (log_path, ec);
    }
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

} // namespace

// Regression test: agents legitimately send requests to the client
// (permission prompts before running commands).  Previously these were
// logged as "Unexpected request from agent" and never answered, so the
// agent stalled forever and session/prompt timed out.
TEST_CASE ("acp: agent permission request is answered so prompt completes")
{
  fake_agent_fixture fx;

  // The fake agent requests permission mid-prompt and only finishes
  // the turn when the client selects an option.
  CHECK (fx.session.prompt ("roll a ball across the scene"));

  const std::string log = fx.read_log ();
  CHECK (log.find ("permission_reply:") != std::string::npos);
  CHECK (log.find ("\"selected\"") != std::string::npos);
  CHECK (log.find ("\"opt-allow\"") != std::string::npos);
}

TEST_CASE ("acp: fs/read_text_file returns requested file contents")
{
  std::filesystem::path target
      = std::filesystem::temp_directory_path () / "weasel_fake_read.txt";
  {
    std::ofstream out (target);
    out << "make the ball roll and bounce\nline two\n";
  }

  fake_agent_fixture fx (target.string ());

  CHECK (fx.session.prompt ("read the file"));

  const std::string log = fx.read_log ();
  CHECK (log.find ("fs_read_reply:") != std::string::npos);
  CHECK (log.find ("roll and bounce") != std::string::npos);

  std::error_code ec;
  std::filesystem::remove (target, ec);
}

TEST_CASE ("acp: unhandled agent request receives an error response")
{
  fake_agent_fixture fx;

  // The fake agent probes an unknown method first; the client must
  // answer with a JSON-RPC error rather than stalling the agent.
  CHECK (fx.session.prompt ("hello"));

  const std::string log = fx.read_log ();
  CHECK (log.find ("-32601") != std::string::npos);
  CHECK (log.find ("Method not handled by client") != std::string::npos);
}
