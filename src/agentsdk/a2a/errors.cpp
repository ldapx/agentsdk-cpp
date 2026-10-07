#include <agentsdk/a2a/errors.hpp>

namespace agentsdk::a2a
{

const char *
error_code_name (error_code code)
{
  switch (code) {
  case error_code::task_not_found:
    return "TaskNotFoundError";
  case error_code::task_not_cancelable:
    return "TaskNotCancelableError";
  case error_code::push_notification_not_supported:
    return "PushNotificationNotSupportedError";
  case error_code::unsupported_operation:
    return "UnsupportedOperationError";
  case error_code::content_type_not_supported:
    return "ContentTypeNotSupportedError";
  case error_code::invalid_agent_response:
    return "InvalidAgentResponseError";
  case error_code::extended_agent_card_not_configured:
    return "ExtendedAgentCardNotConfiguredError";
  case error_code::extension_support_required:
    return "ExtensionSupportRequiredError";
  case error_code::version_not_supported:
    return "VersionNotSupportedError";
  case error_code::parse_error:
    return "JSONParseError";
  case error_code::invalid_request:
    return "InvalidRequestError";
  case error_code::method_not_found:
    return "MethodNotFoundError";
  case error_code::invalid_params:
    return "InvalidParamsError";
  case error_code::internal_error:
    return "InternalError";
  }
  return "UnknownError";
}

int32_t
error_code_http_status (error_code code)
{
  switch (code) {
  case error_code::task_not_found:
    return 404;
  case error_code::task_not_cancelable:
    return 400;
  case error_code::push_notification_not_supported:
    return 400;
  case error_code::unsupported_operation:
    return 400;
  case error_code::content_type_not_supported:
    return 400;
  case error_code::invalid_agent_response:
    return 500;
  case error_code::extended_agent_card_not_configured:
    return 400;
  case error_code::extension_support_required:
    return 400;
  case error_code::version_not_supported:
    return 400;
  case error_code::parse_error:
    return 400;
  case error_code::invalid_request:
    return 400;
  case error_code::method_not_found:
    return 404;
  case error_code::invalid_params:
    return 400;
  case error_code::internal_error:
    return 500;
  }
  return 500;
}

} // namespace agentsdk::a2a
