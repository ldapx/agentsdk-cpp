#include <agentsdk/a2a/client.hpp>
#include <agentsdk/a2a/result.hpp>

namespace agentsdk::a2a
{

a2a_client::a2a_client (std::unique_ptr<client_transport> transport)
    : m_transport (std::move (transport))
{
}

result<send_message_response, a2a_error>
a2a_client::send_message (const send_message_request &request)
{
  return m_transport->send_message (request);
}

result<task, a2a_error>
a2a_client::get_task (const std::string &task_id,
                      std::optional<int32_t> history_length)
{
  get_task_request req;
  req.id = task_id;
  req.history_length = history_length;
  return m_transport->get_task (req);
}

result<list_tasks_response, a2a_error>
a2a_client::list_tasks (const list_tasks_request &request)
{
  return m_transport->list_tasks (request);
}

result<task, a2a_error>
a2a_client::cancel_task (const std::string &task_id)
{
  cancel_task_request req;
  req.id = task_id;
  return m_transport->cancel_task (req);
}

std::unique_ptr<stream_handle>
a2a_client::send_streaming_message (const send_message_request &request,
                                    stream_observer &observer)
{
  return m_transport->send_streaming_message (request, observer);
}

std::unique_ptr<stream_handle>
a2a_client::subscribe_task (const std::string &task_id,
                            stream_observer &observer)
{
  return m_transport->subscribe_task (task_id, observer);
}

} // namespace agentsdk::a2a
