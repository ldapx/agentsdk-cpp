#include <agentsdk/a2a/server.hpp>

#include <agentsdk/a2a/http/http_server.hpp>

namespace agentsdk::a2a
{

a2a_server::a2a_server (agent_card card,
                        std::shared_ptr<request_handler> handler)
    : m_transport (std::make_unique<http_json_server_transport> (
          std::move (card), std::move (handler)))
{
}

a2a_server::~a2a_server () { stop (); }

void
a2a_server::serve (uint16_t port)
{
  // Blocks in the accept loop until stop() flips the transport's
  // running flag.
  m_transport->serve (port);
}

void
a2a_server::serve_async (uint16_t port)
{
  if (m_thread.joinable ()) {
    // Restart semantics: a second serve_async() replaces the
    // previous background instance instead of terminating on
    // assignment to a joinable thread.
    m_transport->stop ();
    m_thread.join ();
  }
  m_thread = std::thread ([this, port] () { serve (port); });
}

void
a2a_server::stop ()
{
  m_transport->stop ();
  if (m_thread.joinable ()) {
    m_thread.join ();
  }
}

bool
a2a_server::is_running () const
{
  return m_transport->is_running ();
}

} // namespace agentsdk::a2a
