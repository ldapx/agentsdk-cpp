# AgentSDK C++

A small, dependency-light C++20 SDK for talking to AI agents over three
standard protocols:

- **A2A** (Agent-to-Agent, [a2a-protocol](https://a2a-protocol.org)) — agent
  discovery, task submission and status polling against remote agents, plus a
  server that hosts an agent card and dispatches requests.
- **ACP** (Agent Client Protocol) — driving a local coding agent as a
  subprocess over newline-delimited JSON-RPC on stdio, with session state,
  tool-call updates and permission prompts.
- **MCP** (Model Context Protocol,
  [modelcontextprotocol.io](https://modelcontextprotocol.io)) — consuming
  tools, resources and prompts from an MCP server subprocess, and hosting
  them for external MCP clients, over newline-delimited JSON-RPC on stdio.

The library is a plain static library with no editor or UI dependencies: you
link it into any C++20 program.

## Features

- **A2A client & server** with a pluggable transport layer.
  - HTTP + JSON transport (libcurl) for `message:send`, task lookup, task
    listing and task cancellation.
  - A dependency-free POSIX listener for the server side — libcurl has no
    server API, so the socket plumbing is ~250 lines rather than a web
    framework.
  - Agent card discovery at `/.well-known/agent-card.json`, with an optional
    in-memory TTL cache (`discovery.hpp`).
  - Streaming is modelled by the `stream_observer` / `stream_handle`
    interfaces, with an SSE frame parser (`sse_parser`) and an in-process
    `local_stream`. The HTTP transport's streaming methods are still stubs.
  - An explicit `result<T, error>` type: no exceptions in the protocol paths.
- **ACP client** for stdio agents.
  - Correlated request/response by JSON-RPC id (never by FIFO position),
    notifications, agent-initiated requests and permission prompts.
  - `acp_session` layers a conversation on top of `acp_client`, and
    `acp_agent_manager` discovers agents on `PATH` and launches them.
- **MCP client & server** for stdio servers (protocol `2025-06-18`, with
  fallback to `2025-03-26` / `2024-11-05` during version negotiation).
  - `initialize` handshake plus `notifications/initialized`, capability
    negotiation and `ping`.
  - Tools (`tools/list` with pagination helpers, `tools/call` with
    `isError` results), resources (`resources/list`, `resources/read`,
    `resources/templates/list`, subscribe/unsubscribe), prompts
    (`prompts/list`, `prompts/get`), `completion/complete` and
    `logging/setLevel` with `notifications/message` delivery.
  - Server-to-client requests in both directions: `sampling/createMessage`,
    `roots/list` and `elicitation/create`, answered on a dispatch thread.
  - Progress (`notifications/progress`) and cancellation
    (`notifications/cancelled`) notifications; timed-out requests emit
    cancellation automatically.
- **JSON layer** built on simdjson, with bidirectional serialization between
  the protocol types and `simdjson::dom::element` (`a2a/json_util.hpp`,
  `mcp/mcp_json.hpp`), and a shared `agentsdk::json_builder` plus a shared
  `result<T, error>` type with no exceptions in the protocol paths.

## Project structure

```
agentsdk-cpp/
├── src/agentsdk/
│   ├── common/              # Shared result<T,E>, JSON builder, stdio
│   │                       # JSON-RPC subprocess base
│   ├── a2a/                 # Agent-to-Agent protocol
│   │   └── http/            # HTTP client transport, server and listener,
│   │                       # JSON-RPC server
│   ├── acp/                 # Agent Client Protocol
│   └── mcp/                 # Model Context Protocol (client + server)
└── tests/agentsdk-core/     # doctest suites + fake ACP/MCP binaries
```

Public headers are included as `<agentsdk/a2a/...>`, `<agentsdk/acp/...>`
and `<agentsdk/mcp/...>`; everything lives in namespace `agentsdk::a2a` /
`agentsdk::acp` / `agentsdk::mcp`, with shared utilities directly under
`agentsdk::`.

## Building

### Prerequisites

- **xmake 2.8.0+**
- **Clang** (the supported toolchain; GCC also works)
- **C++20 compatible compiler**

Dependencies (`spdlog`, `libcurl`, `simdjson`, `doctest`) are fetched
automatically by xmake.

### Build and test

```bash
xmake f --toolchain=clang
xmake build -j2
xmake test
```

## Using it from another project

The library is built with xmake and is not published to xrepo yet. Add a
package definition to your root `xmake.lua` so the target below can be
required by name:

```lua
package("agentsdk")
    set_kind("library")
    set_homepage("https://github.com/ldapx/agentsdk-cpp")
    set_description("AgentSDK C++ — A2A and ACP client SDK")
    set_license("MIT")
    add_urls("https://github.com/ldapx/agentsdk-cpp.git")
    add_versions("dev", "dev")
    on_install(function (package)
        io.writefile("xmake.lua", [[
            add_rules("mode.debug", "mode.release")
            set_languages("c++20")
            add_requires("spdlog", "libcurl", "simdjson")
            target("agentsdk")
                set_kind("static")
                add_files("src/agentsdk/**.cpp")
                add_includedirs("src", {public = true})
                add_packages("spdlog", "libcurl", "simdjson", {public = true})
                if is_plat("linux") then
                    add_syslinks("pthread")
                elseif is_plat("windows") then
                    add_syslinks("ws2_32", "wsock32")
                end
        ]])
        import("package.tools.xmake").install(package)
        -- Preserve the src/agentsdk/... layout under include/ so consumers
        -- keep using <agentsdk/a2a/...>. add_headerfiles would flatten every
        -- header into a single include/ directory.
        os.cp("src/agentsdk", package:installdir("include"))
    end)
    on_load(function (package)
        package:add("includedirs", "include")
    end)
package_end()
```

Then require and link it:

```lua
add_requires("agentsdk")
target("myapp")
    add_packages("agentsdk")
```

`simdjson` and `libcurl` are exported as public packages because their types
appear in the public headers (`a2a/json_util.hpp`, `a2a/http/http_client.hpp`).
`spdlog` is used internally only and is not exposed through the public headers;
if you link a compiled spdlog, export `SPDLOG_COMPILED_LIB` yourself so the SDK
TUs agree with yours.

## Examples

### ACP — drive a local coding agent

```cpp
#include <agentsdk/acp/acp_agent_manager.hpp>
#include <agentsdk/acp/acp_client.hpp>
#include <agentsdk/acp/acp_session.hpp>

using namespace agentsdk::acp;

int main ()
{
  acp_client client;
  acp_session session{ client };

  // A known agent, or use acp_agent_manager::discover_agents ().
  if (!client.launch_agent ("opencode", { "acp" }))
    return 1;

  if (!session.initialize (client_capabilities{}, implementation_info{}))
    return 1;
  if (session.new_session (std::filesystem::current_path ().string ()).empty ())
    return 1;

  session.prompt ("Add a bloom pass to the renderer");
  return 0;
}
```

`session/update` notifications are delivered to
`acp_session::set_update_handler` as raw JSON params; `acp_types.hpp` provides
the matching structs (`tool_call_update`, `plan_entry`, `usage_update`) for the
individual update kinds.

### MCP — call tools on a local server

```cpp
#include <agentsdk/mcp/mcp_client.hpp>

using namespace agentsdk::mcp;

int main ()
{
  mcp_client client;
  if (!client.launch_server ("my-mcp-server", {}))
    return 1;

  implementation_info info{ .name = "myapp", .version = "1.0" };
  client_capabilities caps;
  caps.sampling = true;
  caps.elicitation = true;

  auto init = client.initialize (info, caps);
  if (!init)
    return 1;

  auto tools = client.list_all_tools ();
  if (!tools)
    return 1;

  auto result = client.call_tool ("get_weather", R"({"location":"NYC"})");
  if (!result || result->is_error)
    return 1;
  return 0;
}
```

### MCP — host tools for external clients

```cpp
#include <agentsdk/mcp/mcp_server.hpp>

using namespace agentsdk::mcp;

int main ()
{
  implementation_info info{ .name = "my-server", .version = "1.0" };
  server_capabilities caps;
  caps.tools = tools_capability{};

  mcp_server server{ info, caps };
  server.set_list_tools_handler ([](const std::optional<std::string> &) {
    tools_list_result page;
    tool t;
    t.name = "ping";
    t.input_schema = "{}";
    page.tools.push_back (t);
    return agentsdk::result<tools_list_result, mcp_error>{ page };
  });
  server.run (); // reads stdin, writes stdout until EOF
  return 0;
}
```

### A2A — call a remote agent

```cpp
#include <agentsdk/a2a/client.hpp>
#include <agentsdk/a2a/http/http_client.hpp>

#include <spdlog/spdlog.h>

using namespace agentsdk::a2a;

int main ()
{
  // Usually read from a fetched agent card.
  agent_interface iface;
  iface.url = "http://localhost:8080";
  iface.protocol_binding = "HTTP+JSON";

  auto transport = std::make_unique<http_json_client_transport> (iface);
  a2a_client client{ std::move (transport) };

  send_message_request request;
  request.msg.role = role::user;
  request.msg.parts.push_back (part{ .text = "Summarize the last task" });

  // The response is a std::variant<task, message>.
  auto response = client.send_message (request);
  if (response)
    handle (response.value ());
  else
    spdlog::error ("A2A call failed: {}", response.error ().message);
}
```

### A2A — serve an agent

```cpp
#include <agentsdk/a2a/server.hpp>

a2a_server server{ my_card, std::make_shared<my_handler> () };
server.serve_async (8080);
// ...
server.stop ();
```

## Platform support

Linux and macOS are the primary targets. The A2A listener
(`a2a/http/http_listener.*`), the subprocess management
(`common/stdio_json_rpc.*`, used by `acp/acp_client.*`,
`mcp/mcp_client.*` and `mcp/mcp_server.*`) and the discovery scanner
(`acp/acp_agent_manager.*`)
use POSIX APIs (`fork`/`pipe`/`select`, `PATH` scanning) without platform
guards today, so those areas need porting before Windows builds. Only the
stdio transport is implemented for MCP; Streamable HTTP is not.

## Code style

All code follows [`CODESTYLE.md`](CODESTYLE.md) (GCC conventions with the
project's specific rules) and the matching `.clang-format`.

## License

MIT — see [LICENSE](LICENSE).
