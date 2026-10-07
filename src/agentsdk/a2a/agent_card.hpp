#pragma once

#include <optional>
#include <string>
#include <vector>

namespace agentsdk::a2a
{

/**
 * Service provider information for an agent.
 */
struct agent_provider
{
  /** Provider website URL. */
  std::string url;
  /** Organization name. */
  std::string organization;
};

/**
 * Declares optional capabilities of an agent.
 */
struct agent_capabilities
{
  /** Supports streaming (SSE). */
  bool streaming = false;
  /** Supports push notifications (webhooks). */
  bool push_notifications = false;
  /** Supports authenticated extended agent card. */
  bool extended_agent_card = false;
};

/**
 * A discrete capability exposed by an agent.
 */
struct agent_skill
{
  /** Unique skill identifier. */
  std::string id;
  /** Human-readable skill name. */
  std::string name;
  /** Detailed description of what the skill does. */
  std::string description;
  /** Keywords for discovery. */
  std::vector<std::string> tags;
  /** Example prompts that invoke this skill. */
  std::vector<std::string> examples;
  /** Supported input media types (overrides agent defaults). */
  std::vector<std::string> input_modes;
  /** Supported output media types (overrides agent defaults). */
  std::vector<std::string> output_modes;
};

/**
 * A transport endpoint exposed by the agent.
 */
struct agent_interface
{
  /** Endpoint URL (e.g. "http://localhost:8080"). */
  std::string url;
  /** Protocol binding: "JSONRPC" or "HTTP+JSON". */
  std::string protocol_binding;
  /** Optional routing identifier for multi-tenant agents. */
  std::optional<std::string> tenant;
  /** Protocol version (e.g. "1.0"). */
  std::string protocol_version = "1.0";
};

/**
 * Agent card describing an A2A-compatible agent.
 *
 * Published at ``GET https://{domain}/.well-known/agent-card.json``.
 * Clients use the card to discover the agent's capabilities, transport
 * interfaces, and authentication requirements before interacting.
 */
struct agent_card
{
  /** Human-readable agent name. */
  std::string name;
  /** Human-readable description. */
  std::string description;
  /** Ordered list of supported transport interfaces. First is preferred. */
  std::vector<agent_interface> supported_interfaces;
  /** Service provider information. */
  std::optional<agent_provider> provider;
  /** Agent version string (e.g. "1.0.0"). */
  std::string version;
  /** URL to agent documentation. */
  std::optional<std::string> documentation_url;
  /** Declared agent capabilities. */
  agent_capabilities capabilities;
  /** Default accepted input media types. */
  std::vector<std::string> default_input_modes = { "text/plain" };
  /** Default output media types. */
  std::vector<std::string> default_output_modes = { "text/plain" };
  /** Agent skills. */
  std::vector<agent_skill> skills;
  /** URL to agent icon. */
  std::optional<std::string> icon_url;
};

} // namespace agentsdk::a2a
