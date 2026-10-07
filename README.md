# AgentSDK C++

A small, dependency-light C++20 SDK for talking to AI agents over two
standard protocols:

- **A2A** (Agent-to-Agent, [a2a-protocol](https://a2a-protocol.org)) — agent
  discovery, task submission, status polling and SSE streaming between remote
  agents.
- **ACP** (Agent Client Protocol) — driving a local coding agent as a
  subprocess over newline-delimited JSON-RPC on stdio, with session state,
  tool-call updates, plans and permission prompts.

The library has no engine, editor or UI dependencies: it is a plain static
library you can link into any C++20 program. It was extracted from the
[Weasel Engine](https://github.com/ldapx/weasel) AI-assistance code and is
used there for the editor chat panel.

## Features

- **A2A client & server** with a pluggable transport layer.
  - HTTP + JSON transport (libcurl) with `curl_multi`-based SSE streaming.
  - A dependency-free POSIX listener for the server side — libcurl has no
    server API, so the socket plumbing is ~250 lines rather than a web
    framework.
  - Agent card discovery at `/.well-known/agent-card.json`, including a
    well-known-directory walker (`discovery.hpp`).
  - An explicit `result<T, error>` type: no exceptions in the protocol paths.
- **ACP client** for stdio agents.
  - Correlated request/response by JSON-RPC id (never by FIFO position),
    notifications, agent-initiated requests and permission prompts.
  - `acp_session` layers a conversation on top of `acp_client`, and
    `acp_agent_manager` discovers agents on `PATH` and launches them.
- **JSON layer** built on simdjson, with bidirectional serialization between
  the protocol types and `simdjson::dom::element` (`a2a/json_util.hpp`).

## Project structure

```
agentsdk-cpp/
├── src/agentsdk/
│   ├── a2a/                 # Agent-to-Agent protocol
│   │   └── http/            # HTTP client / server / JSON-RPC transports
│   └── acp/                 # Agent Client Protocol
└── tests/agentsdk-core/     # doctest suites + fake ACP agent binary
```

Public headers are included as `<agentsdk/a2a/...>` and `<agentsdk/acp/...>`;
everything lives in namespace `agentsdk::a2a` / `agentsdk::acp`.

## Building

### Prerequisites

- **xmake 3.0+**
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

### xmake

Declare the dependency from the GitHub URL and link the `agentsdk` package:

```lua
add_requires("agentsdk")
target("myapp")
    add_packages("agentsdk")
```

If the package is not on xrepo yet, add a local package definition to your
`xmake.lua`:

```lua
package("agentsdk")
    set_kind("library")
    set_homepage("https://github.com/ldapx/agentsdk-cpp")
    set_description("AgentSDK C++ — A2A and ACP client SDK")
    set_license("MIT")
    add_urls("https://github.com/ldapx/agentsdk-cpp.git")
    add_versions("main", "main")
    on_install(function (package)
        io.writefile("xmake.lua", [[
            add_rules("mode.debug", "mode.release")
            set_languages("c++20")
            add_requires("spdlog", "libcurl", "simdjson")
            target("agentsdk")
                set_kind("static")
                add_files("src/agentsdk/**.cpp")
                add_headerfiles("src/agentsdk/(a2a/**.hpp, acp/**.hpp)")
                add_includedirs("src", {public = true})
                add_packages("spdlog", "libcurl", "simdjson", {public = true})
                if is_plat("linux") then
                    add_syslinks("pthread")
                end
        ]])
        import("package.tools.xmake").install(package)
    end)
    on_load(function (package)
        package:add("includedirs", "src")
    end)
package_end()
```

`simdjson` and `libcurl` are exported as public packages because their types
appear in the public headers (`a2a/json_util.hpp`, `a2a/http/http_client.hpp`).

### CMake

The sources are plain C++20 with no generated headers, so `add_subdirectory`
or a manual `target_sources` works:

```cmake
add_subdirectory(agentsdk-cpp)
target_link_libraries(myapp PRIVATE agentsdk)
```

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
  request.message.parts.push_back (part{ .text = "Summarize the last task" });

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
(`a2a/http/http_listener.*`), the ACP subprocess management
(`acp/acp_client.*`) and the discovery scanner (`acp/acp_agent_manager.*`)
use POSIX APIs (`fork`/`pipe`/`select`, `PATH` scanning) without platform
guards today, so those three areas need porting before Windows builds.

## Code style

All code follows [`CODESTYLE.md`](CODESTYLE.md) (GCC conventions with the
project's specific rules) and the matching `.clang-format`.

## License

MIT — see [LICENSE](LICENSE).