// Fake ACP agent subprocess used by test_acp_client.cpp.
//
// Speaks newline-delimited JSON-RPC 2.0 over stdio like a real ACP
// v1 agent.  Requests and responses flow in both directions, so —
// like a real agent — replies are correlated by id, never by FIFO
// position.
//
// Exercise sequence:
// - initialize / session/new: answered normally.
// - unknown method probe: sent right after initialize; the client
//   must answer with a JSON-RPC error (-32601).
// - session/prompt: sends fs/read_text_file and then
//   session/request_permission, waits for each response by id, and
//   only completes the turn when permission is granted.
//
// Every response received from the client is appended to the log file
// given by the FAKE_AGENT_LOG environment variable so the tests can
// assert on their payloads.

#include <simdjson.h>

#include <cstdlib>
#include <deque>
#include <fstream>
#include <iostream>
#include <map>
#include <optional>
#include <string>
#include <string_view>

namespace
{

std::ofstream log_file;

void
log_reply (const std::string &label, const std::string &line)
{
  if (!log_file.is_open ()) {
    return;
  }
  log_file << label << ": " << line << std::endl;
}

bool
read_line (std::string &out)
{
  out.clear ();
  int c;
  while ((c = std::getchar ()) != EOF) {
    if (c == '\n') {
      return !out.empty ();
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

std::string
result_for_id (int64_t id, const std::string &result_json)
{
  return "{\"jsonrpc\":\"2.0\",\"id\":" + std::to_string (id)
         + ",\"result\":" + result_json + "}";
}

std::string
error_for_id (int64_t id, const std::string &message)
{
  return "{\"jsonrpc\":\"2.0\",\"id\":" + std::to_string (id)
         + ",\"error\":{\"code\":-1,\"message\":\"" + message + "\"}}";
}

bool
mentions_id (const std::string &message, int64_t id)
{
  const std::string needle = "\"id\":" + std::to_string (id);
  size_t pos = message.find (needle);
  while (pos != std::string::npos) {
    const char next = message[pos + needle.size ()];
    if (next == ',' || next == '}' || next == '\0') {
      return true;
    }
    pos = message.find (needle, pos + 1);
  }
  return false;
}

// Single-threaded full-duplex demux: client requests are answered
// inline as they are read; responses to our own requests are queued
// by id until awaited.
std::map<int64_t, std::string> received_responses;

std::optional<int64_t>
extract_id (const simdjson::simdjson_result<simdjson::dom::element> &doc)
{
  auto id_el = doc["id"];
  if (id_el.error () || !id_el.value ().is_int64 ()) {
    return std::nullopt;
  }
  return id_el.get_int64 ().value_unsafe ();
}

void handle_client_request (int64_t id, std::string_view method);

// Reads messages until a response for ``id`` shows up, answering any
// interleaved requests from the client along the way.
std::optional<std::string>
wait_for_response (int64_t id)
{
  const auto cached = received_responses.find (id);
  if (cached != received_responses.end ()) {
    std::string message = cached->second;
    received_responses.erase (cached);
    return message;
  }

  std::string line;
  while (read_line (line)) {
    simdjson::dom::parser parser;
    auto doc = parser.parse (line);

    auto method_el = doc["method"];
    if (!method_el.error ()) {
      // A request from the client arrived while we were waiting;
      // answer it inline and keep waiting.
      const auto request_id = extract_id (doc);
      handle_client_request (request_id.value_or (-1),
                             method_el.get_string ().value_unsafe ());
      continue;
    }
    const auto response_id = extract_id (doc);
    if (!response_id) {
      continue;
    }

    log_reply ("response", line);
    if (*response_id == id) {
      return line;
    }
    received_responses[*response_id] = line;
  }

  return std::nullopt;
}

void
handle_client_request (int64_t id, std::string_view method)
{
  if (method == "initialize") {
    write_message (result_for_id (
        id, R"({"protocolVersion":1,"agentCapabilities":{},)"
            R"("agentInfo":{"name":"fake-acp-agent","version":"1.0"}})"));
    return;
  }

  if (method == "session/new") {
    write_message (result_for_id (id, R"({"sessionId":"fake-session"})"));
    return;
  }

  if (method == "session/prompt") {
    // Ask the client for a file's contents first.
    const char *read_target = std::getenv ("FAKE_AGENT_READ_TARGET");
    if (read_target) {
      write_message (std::string{ R"({"jsonrpc":"2.0","id":901,)" }
                     + R"("method":"fs/read_text_file","params":{)"
                     + R"("sessionId":"fake-session",)" + "\"path\":\""
                     + read_target + "\"}}");

      const auto reply = wait_for_response (901);
      if (!reply) {
        std::exit (1);
      }
      log_reply ("fs_read_reply", *reply);

      if (reply->find ("\"content\"") == std::string::npos
          || reply->find ("roll and bounce") == std::string::npos) {
        write_message (error_for_id (id, "client did not provide contents"));
        return;
      }
    }

    // Then request permission like an agent would before running a
    // command; the turn only completes once it is granted.
    write_message (
        R"({"jsonrpc":"2.0","id":900,"method":)"
        R"("session/request_permission","params":{"sessionId":)"
        R"("fake-session","toolCall":{"toolCallId":"t1","title":)"
        R"("Run bash"},"options":[{"optionId":"opt-reject",)"
        R"("name":"Reject once","kind":"reject_once"},{"optionId":)"
        R"("opt-allow","name":"Allow once","kind":"allow_once"}]}})");

    const auto reply = wait_for_response (900);
    if (!reply) {
      std::exit (1);
    }
    log_reply ("permission_reply", *reply);

    const bool granted = reply->find ("\"selected\"") != std::string::npos
                         && reply->find ("\"opt-allow\"") != std::string::npos;
    if (!granted) {
      write_message (error_for_id (id, "permission not granted"));
      return;
    }

    write_message (result_for_id (id, R"({"stopReason":"end_turn"})"));
    return;
  }

  write_message (error_for_id (id, "unexpected method from client"));
}

} // namespace

int
main ()
{
  const char *log_path = std::getenv ("FAKE_AGENT_LOG");
  log_file.open (log_path ? log_path : "/dev/null");

  // Probe how the client reacts to an unknown method: it must reply
  // with a JSON-RPC error instead of leaving us hanging.  The reply
  // may arrive at any time; it is validated via the log.
  write_message (R"({"jsonrpc":"2.0","id":800,"method":)"
                 R"("some/unknown_method","params":{}})");

  std::string line;
  while (read_line (line)) {
    simdjson::dom::parser parser;
    auto doc = parser.parse (line);

    auto method_el = doc["method"];
    if (method_el.error ()) {
      const auto id = extract_id (doc);
      if (id) {
        log_reply ("response", line);
        received_responses[*id] = line;
      }
      continue;
    }

    const auto id = extract_id (doc);
    handle_client_request (id.value_or (-1),
                           method_el.get_string ().value_unsafe ());
  }

  return 0;
}
