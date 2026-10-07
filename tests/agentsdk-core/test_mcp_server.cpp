// Part of agentsdk_core_tests; the doctest main lives in
// test_main.cpp.
#include "doctest.h"

#include <agentsdk/mcp/mcp_json.hpp>
#include <agentsdk/mcp/mcp_server.hpp>

#include <signal.h>
#include <sys/select.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cstdio>
#include <string>

using namespace agentsdk;
using namespace agentsdk::mcp;

namespace
{

// Minimal raw JSON-RPC driver: speaks to the test host over pipes and
// answers server->client requests inline with canned responses.
struct raw_host
{
  int write_fd = -1;
  FILE *read_file = nullptr;
  pid_t pid = -1;
  int64_t next_id = 1;

  bool
  launch (const char *path)
  {
    int to_child[2];
    int from_child[2];
    if (pipe (to_child) != 0 || pipe (from_child) != 0) {
      return false;
    }
    pid = fork ();
    if (pid < 0) {
      return false;
    }
    if (pid == 0) {
      dup2 (to_child[0], STDIN_FILENO);
      dup2 (from_child[1], STDOUT_FILENO);
      close (to_child[0]);
      close (to_child[1]);
      close (from_child[0]);
      close (from_child[1]);
      execl (path, path, nullptr);
      _exit (1);
    }
    close (to_child[0]);
    close (from_child[1]);
    write_fd = to_child[1];
    read_file = fdopen (from_child[0], "r");
    return read_file != nullptr;
  }

  ~raw_host () { terminate (); }

  void
  terminate ()
  {
    if (write_fd >= 0) {
      close (write_fd);
      write_fd = -1;
    }
    if (read_file) {
      fclose (read_file);
      read_file = nullptr;
    }
    if (pid > 0) {
      int status = 0;
      // Give it a moment, then escalate.
      for (int i = 0; i < 50; ++i) {
        if (waitpid (pid, &status, WNOHANG) == pid) {
          pid = -1;
          return;
        }
        usleep (10000);
      }
      kill (pid, SIGKILL);
      waitpid (pid, &status, 0);
      pid = -1;
    }
  }

  void
  send_line (const std::string &line)
  {
    std::string msg = line + "\n";
    size_t off = 0;
    while (off < msg.size ()) {
      ssize_t n = write (write_fd, msg.data () + off, msg.size () - off);
      REQUIRE (n > 0);
      off += static_cast<size_t> (n);
    }
  }

  // Read one line with a 10s timeout so a hung host fails loudly.
  std::string
  read_line ()
  {
    int fd = fileno (read_file);
    fd_set fds;
    FD_ZERO (&fds);
    FD_SET (fd, &fds);
    struct timeval timeout{ 10, 0 };
    int ready = select (fd + 1, &fds, nullptr, nullptr, &timeout);
    REQUIRE (ready > 0);

    char *line = nullptr;
    size_t cap = 0;
    ssize_t n = getline (&line, &cap, read_file);
    std::string out;
    if (n > 0) {
      out.assign (line, static_cast<size_t> (n));
      while (!out.empty () && (out.back () == '\n' || out.back () == '\r')) {
        out.pop_back ();
      }
    }
    free (line);
    REQUIRE (!out.empty ());
    return out;
  }

  void
  answer_server_request (int64_t id, const std::string &method)
  {
    std::string result;
    if (method == "sampling/createMessage") {
      result = R"({"role":"assistant","content":{"type":"text",)"
               R"("text":"mock"},"model":"mock-model"})";
    } else if (method == "elicitation/create") {
      result = R"({"action":"accept","content":{"name":"Bob"}})";
    } else if (method == "roots/list") {
      result = R"({"roots":[{"uri":"file:///r","name":"R"},)"
               R"({"uri":"file:///s"}]})";
    } else {
      result = "{}";
    }
    send_line ("{\"jsonrpc\":\"2.0\",\"id\":" + std::to_string (id)
               + ",\"result\":" + result + "}");
  }

  struct rpc_reply
  {
    std::string result;
    std::string error;
  };

  rpc_reply
  call (const std::string &method, const std::string &params)
  {
    int64_t id = next_id++;
    send_line ("{\"jsonrpc\":\"2.0\",\"id\":" + std::to_string (id)
               + ",\"method\":\"" + method + "\",\"params\":" + params + "}");
    while (true) {
      std::string line = read_line ();
      auto parsed = parse_json (line);
      REQUIRE (parsed.has_value ());
      simdjson::dom::element doc = parsed.value ();

      auto method_el = doc["method"];
      auto id_el = doc["id"];
      if (!method_el.error () && !id_el.error ()) {
        // Server-initiated request: answer inline and keep waiting.
        int64_t rid = 0;
        REQUIRE (id_el.value ().get_int64 ().get (rid) == 0);
        answer_server_request (rid,
                               std::string (method_el.get_string ().value ()));
        continue;
      }
      if (!method_el.error ()) {
        // Notification: ignore.
        continue;
      }
      int64_t rid = -1;
      if (!id_el.error ()) {
        if (id_el.value ().get_int64 ().get (rid) != 0) {
          rid = -1;
        }
      }
      REQUIRE (rid == id);
      rpc_reply reply;
      auto res_el = doc["result"];
      if (!res_el.error ()) {
        reply.result = simdjson::to_string (res_el.value ());
      }
      auto err_el = doc["error"];
      if (!err_el.error ()) {
        reply.error = simdjson::to_string (err_el.value ());
      }
      return reply;
    }
  }

  void
  notify (const std::string &method, const std::string &params)
  {
    send_line ("{\"jsonrpc\":\"2.0\",\"method\":\"" + method
               + "\",\"params\":" + params + "}");
  }

  void
  initialize (const std::string &version = "2025-06-18")
  {
    std::string params = "{\"protocolVersion\":\"" + version
                         + "\",\"capabilities\":{\"roots\":{},\"sampling\":"
                           "{},\"elicitation\":{}},\"clientInfo\":{\"name\":"
                           "\"t\",\"version\":\"1\"}}";
    rpc_reply r = call ("initialize", params);
    REQUIRE (r.error.empty ());
    REQUIRE (r.result.find ("protocolVersion") != std::string::npos);
    notify ("notifications/initialized", "{}");
  }
};

} // namespace

TEST_CASE ("mcp-server: unknown version falls back to latest")
{
  raw_host host;
  REQUIRE (host.launch (MCP_TEST_HOST_PATH));

  raw_host::rpc_reply r = host.call (
      "initialize", "{\"protocolVersion\":\"1999-01-01\",\"capabilities\":"
                    "{},\"clientInfo\":{\"name\":\"t\",\"version\":\"1\"}}");
  REQUIRE (r.error.empty ());
  CHECK (r.result.find ("2025-06-18") != std::string::npos);
}

TEST_CASE ("mcp-server: requests before initialize are rejected")
{
  raw_host host;
  REQUIRE (host.launch (MCP_TEST_HOST_PATH));

  raw_host::rpc_reply r = host.call ("tools/list", "{}");
  REQUIRE (!r.error.empty ());
  CHECK (r.error.find ("-32600") != std::string::npos);
}

TEST_CASE ("mcp-server: tools list and call through the real server")
{
  raw_host host;
  REQUIRE (host.launch (MCP_TEST_HOST_PATH));
  host.initialize ();

  raw_host::rpc_reply list = host.call ("tools/list", "{}");
  REQUIRE (list.error.empty ());
  CHECK (list.result.find ("\"add\"") != std::string::npos);

  raw_host::rpc_reply sum
      = host.call ("tools/call", R"({"name":"add","arguments":{"a":2,"b":3}})");
  REQUIRE (sum.error.empty ());
  CHECK (sum.result.find ("sum:5") != std::string::npos);

  raw_host::rpc_reply unknown
      = host.call ("tools/call", R"({"name":"nope","arguments":{}})");
  REQUIRE (!unknown.error.empty ());
  CHECK (unknown.error.find ("-32601") != std::string::npos);
}

TEST_CASE ("mcp-server: resources, prompts, completion, logging, ping")
{
  raw_host host;
  REQUIRE (host.launch (MCP_TEST_HOST_PATH));
  host.initialize ();

  raw_host::rpc_reply resources = host.call ("resources/list", "{}");
  REQUIRE (resources.error.empty ());
  CHECK (resources.result.find ("test://hello") != std::string::npos);

  raw_host::rpc_reply read
      = host.call ("resources/read", R"({"uri":"test://hello"})");
  REQUIRE (read.error.empty ());
  CHECK (read.result.find ("\"hi\"") != std::string::npos);

  raw_host::rpc_reply missing
      = host.call ("resources/read", R"({"uri":"test://nope"})");
  REQUIRE (!missing.error.empty ());
  CHECK (missing.error.find ("-32002") != std::string::npos);

  raw_host::rpc_reply prompt = host.call (
      "prompts/get", R"({"name":"shout","arguments":{"text":"hey"}})");
  REQUIRE (prompt.error.empty ());
  CHECK (prompt.result.find ("shout:hey") != std::string::npos);

  raw_host::rpc_reply completion = host.call (
      "completion/complete", R"({"ref":{"type":"ref/prompt","name":"shout"},)"
                             R"("argument":{"name":"text","value":"h"}})");
  REQUIRE (completion.error.empty ());
  CHECK (completion.result.find ("alpha") != std::string::npos);

  raw_host::rpc_reply bad_level
      = host.call ("logging/setLevel", R"({"level":"verbose"})");
  REQUIRE (!bad_level.error.empty ());
  CHECK (bad_level.error.find ("-32602") != std::string::npos);

  raw_host::rpc_reply level
      = host.call ("logging/setLevel", R"({"level":"error"})");
  REQUIRE (level.error.empty ());

  raw_host::rpc_reply ping = host.call ("ping", "{}");
  REQUIRE (ping.error.empty ());

  raw_host::rpc_reply unknown = host.call ("unknown/method", "{}");
  REQUIRE (!unknown.error.empty ());
  CHECK (unknown.error.find ("-32601") != std::string::npos);
}

TEST_CASE ("mcp-server: elicitation round-trip through the real server")
{
  raw_host host;
  REQUIRE (host.launch (MCP_TEST_HOST_PATH));
  host.initialize ();

  raw_host::rpc_reply r
      = host.call ("tools/call", R"({"name":"need_name","arguments":{}})");
  REQUIRE (r.error.empty ());
  // The host forwards our accepted content into the tool result.
  CHECK (r.result.find ("Bob") != std::string::npos);
}

TEST_CASE ("mcp-server: sampling round-trip through the real server")
{
  raw_host host;
  REQUIRE (host.launch (MCP_TEST_HOST_PATH));
  host.initialize ();

  raw_host::rpc_reply r
      = host.call ("tools/call", R"({"name":"need_sample","arguments":{}})");
  REQUIRE (r.error.empty ());
  CHECK (r.result.find ("sampled:mock") != std::string::npos);
}

TEST_CASE ("mcp-server: roots round-trip through the real server")
{
  raw_host host;
  REQUIRE (host.launch (MCP_TEST_HOST_PATH));
  host.initialize ();

  raw_host::rpc_reply r
      = host.call ("tools/call", R"({"name":"need_roots","arguments":{}})");
  REQUIRE (r.error.empty ());
  CHECK (r.result.find ("roots:2") != std::string::npos);
}

TEST_CASE ("mcp-json: content blocks survive a serialize/parse round-trip")
{
  text_content text;
  text.text = "hello \"world\"\nnewline";
  annotations a;
  a.audience = { "user" };
  a.priority = 0.5;
  text.annot = a;

  content_block block{ text };
  auto parsed = parse_json (to_json (block));
  REQUIRE (parsed.has_value ());
  auto back = content_block_from_json (parsed.value ());
  REQUIRE (back.has_value ());
  const text_content *t = std::get_if<text_content> (&back.value ());
  REQUIRE (t != nullptr);
  CHECK (t->text == "hello \"world\"\nnewline");
  REQUIRE (t->annot.has_value ());
  CHECK (t->annot->priority.value_or (0.0) == doctest::Approx (0.5));

  CHECK (std::string (log_level_name (log_level::warning)) == "warning");
  CHECK (parse_log_level ("error").value_or (log_level::info)
         == log_level::error);
  CHECK (!parse_log_level ("verbose").has_value ());
  CHECK (log_level_at_least (log_level::error, log_level::warning));
  CHECK (!log_level_at_least (log_level::debug, log_level::info));
}

TEST_CASE ("mcp-server: handle_line answers initialize without a subprocess")
{
  implementation_info info;
  info.name = "unit";
  info.version = "1";
  server_capabilities caps;
  caps.tools = tools_capability{};
  mcp_server server (info, caps);

  auto response = server.handle_line (
      R"({"jsonrpc":"2.0","id":1,"method":"initialize","params":{)"
      R"("protocolVersion":"2025-06-18","capabilities":{},)"
      R"("clientInfo":{"name":"c","version":"1"}}})");
  REQUIRE (response.has_value ());
  CHECK (response->find ("2025-06-18") != std::string::npos);
  CHECK (response->find ("\"id\":1") != std::string::npos);
}
