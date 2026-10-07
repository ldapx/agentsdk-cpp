#pragma once

#include <string>
#include <variant>
#include <vector>

#include <agentsdk/a2a/agent_card.hpp>
#include <agentsdk/a2a/errors.hpp>
#include <agentsdk/a2a/types.hpp>

namespace agentsdk::a2a
{

/**
 * Configuration for a send_message request.
 */
struct send_message_configuration
{
  /** Accepted output media types. */
  std::vector<std::string> accepted_output_modes;
  /** Max history messages to include in the response. */
  std::optional<int32_t> history_length;
  /** If ``true``, return immediately without waiting for task completion. */
  bool return_immediately = false;
};

/**
 * Request to send a message to an agent.
 */
struct send_message_request
{
  /** The message to send. */
  message msg;
  /** Optional request configuration. */
  std::optional<send_message_configuration> configuration;
  /** Optional metadata as a raw JSON string. */
  std::optional<std::string> metadata;
};

/**
 * Response from a send_message operation.
 *
 * The agent either returns a task tracking the processing, or a direct
 * message response for simple interactions.
 */
using send_message_response = std::variant<task, message>;

/**
 * Request to get the current state of a task.
 */
struct get_task_request
{
  /** Task ID. */
  std::string id;
  /** Max history messages to include. */
  std::optional<int32_t> history_length;
};

/**
 * Request to list tasks with optional filtering.
 */
struct list_tasks_request
{
  /** Filter by context ID. */
  std::optional<std::string> context_id;
  /** Filter by task state. */
  std::optional<task_state> status;
  /** Page size (1-100, default 50). */
  std::optional<int32_t> page_size;
  /** Cursor for the next page. */
  std::optional<std::string> page_token;
  /** Max history messages per task. */
  std::optional<int32_t> history_length;
  /** Include artifacts in the response (default false). */
  bool include_artifacts = false;
};

/**
 * Response from a list_tasks operation.
 */
struct list_tasks_response
{
  /** Matching tasks. */
  std::vector<task> tasks;
  /** Cursor for the next page. Empty string means no more pages. */
  std::string next_page_token;
  /** Page size used. */
  int32_t page_size = 0;
  /** Total count before pagination. */
  int32_t total_size = 0;
};

/**
 * Request to cancel a running task.
 */
struct cancel_task_request
{
  /** Task ID. */
  std::string id;
  /** Optional metadata as a raw JSON string. */
  std::optional<std::string> metadata;
};

/**
 * Push notification configuration for a task.
 */
struct push_notification_config
{
  /** Config UUID. */
  std::string id;
  /** Associated task ID. */
  std::string task_id;
  /** Webhook URL (required). */
  std::string url;
  /** Unique token for this task/session. */
  std::optional<std::string> token;
  /** HTTP auth scheme (e.g. "Bearer"). */
  std::optional<std::string> auth_scheme;
  /** Auth credentials. */
  std::optional<std::string> auth_credentials;
};

} // namespace agentsdk::a2a
