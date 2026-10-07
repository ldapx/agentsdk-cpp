// Fake MCP server subprocess used by test_mcp_client.cpp.
//
// Speaks newline-delimited JSON-RPC 2.0 over stdio per MCP 2025-06-18.
// Deliberately independent of agentsdk::mcp::mcp_server so the client is
// tested against a second implementation.
//
// Supported methods:
// - initialize / notifications/initialized / ping
// - tools/list (paginated, 2 per page), tools/call (echo, fail, ask_user,
//   sample, show_roots)
// - resources/list, resources/templates/list, resources/read,
//   resources/subscribe, resources/unsubscribe
// - prompts/list, prompts/get
// - completion/complete
// - logging/setLevel (stored; echo calls emit a notifications/message)
//
// ask_user/sample/show_roots exercise server->client requests
// (elicitation/create, sampling/createMessage, roots/list).
//
// Every request received is appended to $FAKE_MCP_LOG for assertions.

#include <simdjson.h>

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <map>
#include <optional>
#include <string>

namespace
{

std::ofstream log_file;

void
log_line (const std::string &label, const std::string &line)
{
  if (log_file.is_open ()) {
    log_file << label << ": " << line << std::endl;
  }
}

bool
read_line (std::string &out)
{
  out.clear ();
  int c;
  while ((c = std::getchar ()) != EOF) {
    if (c == '\n') {
      return true;
    }
    out.push_back (static_cast<char> (c));
  }
  return !out.empty ();
}

void
write_message (const std::string &line)
{
  std::cout << line << '\n';
  std::cout.flush ();
}

// JSON string escaping for embedding values in responses.
std::string
escape (const std::string &value)
{
  std::string out;
  for (char c : value) {
    switch (c) {
    case '"':
      out += "\\\"";
      break;
    case '\\':
      out += "\\\\";
      break;
    case '\n':
      out += "\\n";
      break;
    case '\r':
      out += "\\r";
      break;
    case '\t':
      out += "\\t";
      break;
    default:
      out += c;
      break;
    }
  }
  return out;
}

std::optional<int64_t>
extract_id (const simdjson::simdjson_result<simdjson::dom::element> &doc)
{
  auto id_el = doc["id"];
  if (id_el.error ()) {
    return std::nullopt;
  }
  int64_t id = 0;
  if (id_el.value ().get_int64 ().get (id) != 0) {
    return std::nullopt;
  }
  return id;
}

std::string
result_for_id (int64_t id, const std::string &result_json)
{
  return "{\"jsonrpc\":\"2.0\",\"id\":" + std::to_string (id)
         + ",\"result\":" + result_json + "}";
}

std::string
error_for_id (int64_t id, int64_t code, const std::string &message)
{
  return "{\"jsonrpc\":\"2.0\",\"id\":" + std::to_string (id)
         + ",\"error\":{\"code\":" + std::to_string (code) + ",\"message\":\""
         + escape (message) + "\"}}";
}

// Responses to our own outgoing requests, keyed by id.
std::map<int64_t, std::string> received_responses;
int64_t next_out_id = 1000;

std::optional<std::string>
wait_for_response (int64_t id)
{
  auto cached = received_responses.find (id);
  if (cached != received_responses.end ()) {
    std::string message = cached->second;
    received_responses.erase (cached);
    return message;
  }
  std::string line;
  while (read_line (line)) {
    if (line.empty ()) {
      continue;
    }
    simdjson::dom::parser parser;
    auto doc = parser.parse (line);
    auto method_el = doc["method"];
    if (!method_el.error ()) {
      // No interleaved client requests are expected while awaiting a
      // response in these tests; log and keep waiting.
      log_line ("unexpected", line);
      continue;
    }
    auto rid = extract_id (doc);
    if (!rid) {
      continue;
    }
    log_line ("response", line);
    if (*rid == id) {
      return line;
    }
    received_responses[*rid] = line;
  }
  return std::nullopt;
}

std::string log_level = "debug";

const char *TOOL_ECHO = R"({"name":"echo","description":"Echo text back",)"
                        R"("inputSchema":{"type":"object","properties":{)"
                        R"("text":{"type":"string"}},"required":["text"]}})";
const char *TOOL_FAIL = R"({"name":"fail","description":"Always fails",)"
                        R"("inputSchema":{"type":"object","properties":{}}})";
const char *TOOL_ASK
    = R"({"name":"ask_user","description":"Ask via elicitation",)"
      R"("inputSchema":{"type":"object","properties":{}}})";
const char *TOOL_SAMPLE
    = R"({"name":"sample","description":"Ask via sampling",)"
      R"("inputSchema":{"type":"object","properties":{}}})";
const char *TOOL_ROOTS = R"({"name":"show_roots","description":"List roots",)"
                         R"("inputSchema":{"type":"object","properties":{}}})";

void
handle_request (int64_t id, const std::string &method, const std::string &line)
{
  simdjson::dom::parser parser;
  auto doc = parser.parse (line);
  auto params_el = doc["params"];
  simdjson::dom::element params;
  bool has_params = !params_el.error ();
  if (has_params) {
    params = params_el.value ();
  }

  if (method == "initialize") {
    std::string requested;
    if (has_params) {
      auto v = params["protocolVersion"].get_string ();
      if (!v.error ()) {
        requested = std::string (v.value ());
      }
    }
    if (requested != "2025-06-18" && requested != "2025-03-26"
        && requested != "2024-11-05") {
      requested = "2025-06-18";
    }
    write_message (result_for_id (
        id, "{\"protocolVersion\":\"" + requested
                + "\",\"capabilities\":{"
                  "\"tools\":{\"listChanged\":true},\"resources\":{"
                  "\"subscribe\":true,\"listChanged\":true},\"prompts\":{"
                  "\"listChanged\":true},\"logging\":{},\"completions\":{}},"
                  "\"serverInfo\":{\"name\":\"fake-mcp-server\",\"version\":"
                  "\"1.0\"},\"instructions\":\"Be nice.\"}"));
    return;
  }

  if (method == "ping") {
    write_message (result_for_id (id, "{}"));
    return;
  }

  if (method == "tools/list") {
    std::string cursor;
    if (has_params) {
      auto c = params["cursor"].get_string ();
      if (!c.error ()) {
        cursor = std::string (c.value ());
      }
    }
    if (cursor.empty ()) {
      write_message (result_for_id (id, std::string{ "{\"tools\":[" }
                                            + TOOL_ECHO + "," + TOOL_FAIL
                                            + "],\"nextCursor\":\"page2\"}"));
    } else if (cursor == "page2") {
      write_message (result_for_id (id, std::string{ "{\"tools\":[" } + TOOL_ASK
                                            + "," + TOOL_SAMPLE
                                            + "],\"nextCursor\":\"page3\"}"));
    } else {
      write_message (result_for_id (id, std::string{ "{\"tools\":[" }
                                            + TOOL_ROOTS + "]}"));
    }
    return;
  }

  if (method == "tools/call") {
    std::string name;
    if (has_params) {
      auto n = params["name"].get_string ();
      if (!n.error ()) {
        name = std::string (n.value ());
      }
    }
    if (name == "fail") {
      write_message (result_for_id (
          id, "{\"content\":[{\"type\":\"text\",\"text\":\"it broke\"}],"
              "\"isError\":true}"));
      return;
    }
    if (name == "ask_user") {
      int64_t out_id = next_out_id++;
      write_message (
          "{\"jsonrpc\":\"2.0\",\"id\":" + std::to_string (out_id)
          + ",\"method\":\"elicitation/create\",\"params\":{\"message\":"
            "\"Your name?\",\"requestedSchema\":{\"type\":\"object\","
            "\"properties\":{\"name\":{\"type\":\"string\"}},\"required\":["
            "\"name\"]}}}");
      auto reply = wait_for_response (out_id);
      if (!reply) {
        write_message (error_for_id (id, -32603, "no elicitation reply"));
        return;
      }
      log_line ("elicitation_reply", *reply);
      write_message (result_for_id (
          id, "{\"content\":[{\"type\":\"text\",\"text\":\"" + escape (*reply)
                  + "\"}],\"isError\":false}"));
      return;
    }
    if (name == "sample") {
      int64_t out_id = next_out_id++;
      write_message ("{\"jsonrpc\":\"2.0\",\"id\":" + std::to_string (out_id)
                     + ",\"method\":\"sampling/createMessage\",\"params\":{"
                       "\"messages\":[{\"role\":\"user\",\"content\":{\"type\":"
                       "\"text\",\"text\":\"hi\"}}],\"maxTokens\":16}}");
      auto reply = wait_for_response (out_id);
      if (!reply) {
        write_message (error_for_id (id, -32603, "no sampling reply"));
        return;
      }
      log_line ("sampling_reply", *reply);
      // Echo the sampled text back as the tool result.
      simdjson::dom::parser rparser;
      auto rdoc = rparser.parse (*reply);
      std::string text = "?";
      auto res_el = rdoc["result"];
      if (!res_el.error ()) {
        auto content_el = res_el.value ()["content"];
        if (!content_el.error ()) {
          auto t = content_el.value ()["text"].get_string ();
          if (!t.error ()) {
            text = std::string (t.value ());
          }
        }
      }
      write_message (result_for_id (
          id, "{\"content\":[{\"type\":\"text\",\"text\":\"sampled:"
                  + escape (text) + "\"}],\"isError\":false}"));
      return;
    }
    if (name == "show_roots") {
      int64_t out_id = next_out_id++;
      write_message ("{\"jsonrpc\":\"2.0\",\"id\":" + std::to_string (out_id)
                     + ",\"method\":\"roots/list\",\"params\":{}}");
      auto reply = wait_for_response (out_id);
      if (!reply) {
        write_message (error_for_id (id, -32603, "no roots reply"));
        return;
      }
      log_line ("roots_reply", *reply);
      write_message (result_for_id (
          id, "{\"content\":[{\"type\":\"text\",\"text\":\"" + escape (*reply)
                  + "\"}],\"isError\":false}"));
      return;
    }
    if (name == "echo") {
      std::string text;
      if (has_params) {
        auto args_el = params["arguments"];
        if (!args_el.error ()) {
          auto t = args_el.value ()["text"].get_string ();
          if (!t.error ()) {
            text = std::string (t.value ());
          }
        }
      }
      // Emit a log notification so the client log handler is exercised.
      write_message ("{\"jsonrpc\":\"2.0\",\"method\":"
                     "\"notifications/message\",\"params\":{\"level\":\""
                     + log_level
                     + "\",\"logger\":\"fake\",\"data\":{"
                       "\"text\":\"echo called\"}}}");
      write_message (
          result_for_id (id, "{\"content\":[{\"type\":\"text\",\"text\":\"echo:"
                                 + escape (text) + "\"}],\"isError\":false}"));
      return;
    }
    write_message (error_for_id (id, -32602, "Unknown tool: " + name));
    return;
  }

  if (method == "resources/list") {
    write_message (result_for_id (
        id, "{\"resources\":[{\"uri\":\"file:///notes.txt\",\"name\":"
            "\"notes\",\"mimeType\":\"text/plain\"}]}"));
    return;
  }

  if (method == "resources/templates/list") {
    write_message (result_for_id (
        id, "{\"resourceTemplates\":[{\"uriTemplate\":\"file:///{path}\","
            "\"name\":\"files\"}]}"));
    return;
  }

  if (method == "resources/read") {
    std::string uri;
    if (has_params) {
      auto u = params["uri"].get_string ();
      if (!u.error ()) {
        uri = std::string (u.value ());
      }
    }
    if (uri == "file:///notes.txt") {
      write_message (result_for_id (
          id, "{\"contents\":[{\"uri\":\"file:///notes.txt\",\"mimeType\":"
              "\"text/plain\",\"text\":\"hello resource\"}]}"));
    } else {
      write_message (error_for_id (id, -32002, "Resource not found"));
    }
    return;
  }

  if (method == "resources/subscribe" || method == "resources/unsubscribe") {
    write_message (result_for_id (id, "{}"));
    return;
  }

  if (method == "prompts/list") {
    write_message (result_for_id (
        id, "{\"prompts\":[{\"name\":\"greet\",\"description\":\"Say hi\","
            "\"arguments\":[{\"name\":\"name\",\"required\":true}]}]}"));
    return;
  }

  if (method == "prompts/get") {
    std::string name;
    if (has_params) {
      auto n = params["name"].get_string ();
      if (!n.error ()) {
        name = std::string (n.value ());
      }
    }
    if (name != "greet") {
      write_message (error_for_id (id, -32602, "Unknown prompt"));
      return;
    }
    std::string who = "stranger";
    if (has_params) {
      auto args_el = params["arguments"];
      if (!args_el.error ()) {
        auto w = args_el.value ()["name"].get_string ();
        if (!w.error ()) {
          who = std::string (w.value ());
        }
      }
    }
    write_message (result_for_id (
        id, "{\"messages\":[{\"role\":\"user\",\"content\":{\"type\":"
            "\"text\",\"text\":\"Hello, "
                + escape (who) + "!\"}}]}"));
    return;
  }

  if (method == "completion/complete") {
    std::string value;
    if (has_params) {
      auto arg_el = params["argument"];
      if (!arg_el.error ()) {
        auto v = arg_el.value ()["value"].get_string ();
        if (!v.error ()) {
          value = std::string (v.value ());
        }
      }
    }
    std::string out;
    for (const char *candidate : { "python", "pytorch", "pyside" }) {
      if (std::string (candidate).rfind (value, 0) == 0) {
        if (!out.empty ()) {
          out += ",";
        }
        out += "\"";
        out += candidate;
        out += "\"";
      }
    }
    write_message (result_for_id (id, "{\"completion\":{\"values\":[" + out
                                          + "],\"hasMore\":false}}"));
    return;
  }

  if (method == "logging/setLevel") {
    if (has_params) {
      auto l = params["level"].get_string ();
      if (!l.error ()) {
        log_level = std::string (l.value ());
      }
    }
    write_message (result_for_id (id, "{}"));
    return;
  }

  write_message (error_for_id (id, -32601, "Method not found: " + method));
}

} // namespace

int
main ()
{
  const char *log_path = std::getenv ("FAKE_MCP_LOG");
  log_file.open (log_path ? log_path : "/dev/null");

  std::string line;
  while (read_line (line)) {
    if (line.empty ()) {
      continue;
    }
    log_line ("request", line);
    simdjson::dom::parser parser;
    auto doc = parser.parse (line);

    auto method_el = doc["method"];
    if (method_el.error ()) {
      auto id = extract_id (doc);
      if (id) {
        log_line ("response", line);
        received_responses[*id] = line;
      }
      continue;
    }

    std::string method (method_el.get_string ().value ());
    if (method.rfind ("notifications/", 0) == 0) {
      // notifications/initialized: nothing to do.
      continue;
    }

    auto id = extract_id (doc);
    handle_request (id.value_or (-1), method, line);
  }

  return 0;
}
