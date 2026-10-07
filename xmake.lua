set_project("agentsdk")
set_version("0.1.0")
set_xmakever("2.8.0")

add_rules("mode.debug", "mode.release")
set_languages("c++20")

-- ---------------------------------------------------------------------------
-- Dependencies
-- ---------------------------------------------------------------------------
-- spdlog  : logging facade used by the transports and the ACP client.
-- libcurl : HTTP client transport + agent-card discovery.
-- simdjson: JSON parsing/validation for both JSON-RPC dialects.
--
-- spdlog is deliberately required without a `shared` config: consumers that
-- link a compiled spdlog are expected to export SPDLOG_COMPILED_LIB
-- themselves.  Without that define spdlog compiles header-only, which is
-- what a standalone build of this project gets.
add_requires("spdlog v1.17.0")
add_requires("libcurl")
add_requires("simdjson v4.6.2")

-- always need doctest for tests
add_requires("doctest")

-- ---------------------------------------------------------------------------
-- agentsdk — core agent SDK static library
-- ---------------------------------------------------------------------------
target("agentsdk")
    set_kind("static")
    set_languages("c++20")
    add_files("src/agentsdk/**.cpp")
    add_headerfiles("src/agentsdk/(a2a/**.hpp, acp/**.hpp)")
    add_includedirs("src", {public = true})
    -- simdjson and curl types show up in the public headers
    -- (a2a/json_util.hpp, a2a/http/http_client.hpp), so consumers need them.
    add_packages("spdlog", "libcurl", "simdjson", {public = true})
    add_cxxflags("-Wall", "-Wextra", "-Wpedantic")
    if is_plat("linux") then
        add_syslinks("pthread")
    elseif is_plat("windows") then
        add_syslinks("ws2_32", "wsock32")
    end

-- ---------------------------------------------------------------------------
-- Tests
-- ---------------------------------------------------------------------------
-- Standalone fake ACP agent used by test_acp_client.cpp: it speaks
-- newline-delimited JSON-RPC 2.0 over stdio and records everything it
-- receives in $FAKE_AGENT_LOG.
target("fake_acp_agent")
    set_kind("binary")
    set_languages("c++20")
    add_files("tests/agentsdk-core/fake_acp_agent.cpp")
    add_packages("simdjson")

target("agentsdk_core_tests")
    set_kind("binary")
    set_languages("c++20")
    add_files("tests/agentsdk-core/test_*.cpp")
    add_deps("agentsdk", "fake_acp_agent")
    add_packages("doctest")
    -- Absolute: `xmake test` runs the binary from a different cwd than the
-- project root, so a builddir-relative path only works when invoked by hand.
add_defines("FAKE_AGENT_PATH=\"$(projectdir)/$(builddir)/$(plat)/$(arch)/$(mode)/fake_acp_agent\"")
    add_tests("agentsdk_core_tests")