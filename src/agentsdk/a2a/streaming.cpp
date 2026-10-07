#include <agentsdk/a2a/streaming.hpp>

namespace agentsdk::a2a
{

// ---------------------------------------------------------------------------
// sse_parser
// ---------------------------------------------------------------------------

sse_parser::sse_parser (event_callback callback)
    : m_callback (std::move (callback))
{
  m_buffer.reserve (4096);
}

void
sse_parser::feed (const char *data, size_t length)
{
  m_buffer.append (data, length);

  // SSE events are separated by blank lines (\n\n).
  while (true) {
    auto pos = m_buffer.find ("\n\n");
    if (pos == std::string::npos) {
      break;
    }

    std::string event_block = m_buffer.substr (0, pos);
    m_buffer.erase (0, pos + 2);

    // Extract data lines.
    std::string event_data;
    size_t search_pos = 0;
    while (search_pos < event_block.size ()) {
      auto line_end = event_block.find ('\n', search_pos);
      std::string line;
      if (line_end == std::string::npos) {
        line = event_block.substr (search_pos);
        search_pos = event_block.size ();
      } else {
        line = event_block.substr (search_pos, line_end - search_pos);
        search_pos = line_end + 1;
      }

      if (line.substr (0, 5) == "data:") {
        event_data += line.substr (5);
        // Trim leading space
        if (!event_data.empty () && event_data[0] == ' ') {
          event_data.erase (0, 1);
        }
      }
    }

    if (!event_data.empty () && m_callback) {
      m_callback (event_data);
    }
  }
}

void
sse_parser::finish ()
{
  // Process any remaining data in the buffer.
  if (!m_buffer.empty () && m_callback) {
    m_callback (m_buffer);
    m_buffer.clear ();
  }
}

// ---------------------------------------------------------------------------
// background_stream_handle
// ---------------------------------------------------------------------------

background_stream_handle::background_stream_handle () { m_active.store (true); }

background_stream_handle::~background_stream_handle ()
{
  cancel ();
  if (m_thread.joinable ()) {
    m_thread.join ();
  }
}

void
background_stream_handle::cancel ()
{
  m_active.store (false);
}

bool
background_stream_handle::is_active () const
{
  return m_active.load ();
}

void
background_stream_handle::set_thread (std::thread thread)
{
  m_thread = std::move (thread);
}

// ---------------------------------------------------------------------------
// local_stream
// ---------------------------------------------------------------------------

local_stream::local_stream (stream_observer &observer) : m_observer (observer)
{
}

void
local_stream::cancel ()
{
  m_active.store (false);
}

bool
local_stream::is_active () const
{
  return m_active.load ();
}

void
local_stream::push_event (const stream_response &event)
{
  if (m_active.load ()) {
    m_observer.on_event (event);
  }
}

void
local_stream::complete ()
{
  m_active.store (false);
  m_observer.on_completed ();
}

void
local_stream::error (const a2a_error &err)
{
  m_active.store (false);
  m_observer.on_error (err);
}

} // namespace agentsdk::a2a
