#include <agentsdk/common/json_builder.hpp>

#include <cstdio>

namespace agentsdk
{

json_builder::json_builder () { m_buffer.reserve (256); }

void
json_builder::begin_object ()
{
  m_scopes.push_back (true);
  m_first_in_scope = true;
  m_buffer.push_back ('{');
}

void
json_builder::begin_object (const std::string &key)
{
  append_key (key);
  m_scopes.push_back (true);
  m_first_in_scope = true;
  m_buffer.push_back ('{');
}

void
json_builder::end_object ()
{
  m_buffer.push_back ('}');
  m_scopes.pop_back ();
  m_first_in_scope = false;
}

void
json_builder::begin_array ()
{
  m_scopes.push_back (false);
  m_first_in_scope = true;
  m_buffer.push_back ('[');
}

void
json_builder::begin_array (const std::string &key)
{
  append_key (key);
  m_scopes.push_back (false);
  m_first_in_scope = true;
  m_buffer.push_back ('[');
}

void
json_builder::end_array ()
{
  m_buffer.push_back (']');
  m_scopes.pop_back ();
  m_first_in_scope = false;
}

void
json_builder::append_key (const std::string &key)
{
  if (!m_first_in_scope) {
    m_buffer.push_back (',');
  }
  m_first_in_scope = false;
  m_buffer.push_back ('"');
  m_buffer.append (escape (key));
  m_buffer.append ("\":");
}

std::string
json_builder::escape (const std::string &value)
{
  std::string out;
  out.reserve (value.size () + 2);
  for (unsigned char c : value) {
    switch (c) {
    case '"':
      out.append ("\\\"");
      break;
    case '\\':
      out.append ("\\\\");
      break;
    case '\b':
      out.append ("\\b");
      break;
    case '\f':
      out.append ("\\f");
      break;
    case '\n':
      out.append ("\\n");
      break;
    case '\r':
      out.append ("\\r");
      break;
    case '\t':
      out.append ("\\t");
      break;
    default:
      if (c < 0x20) {
        char buf[7];
        std::snprintf (buf, sizeof (buf), "\\u%04x", c);
        out.append (buf);
      } else {
        out.push_back (static_cast<char> (c));
      }
      break;
    }
  }
  return out;
}

void
json_builder::append_escaped (const std::string &value)
{
  m_buffer.push_back ('"');
  m_buffer.append (escape (value));
  m_buffer.push_back ('"');
}

void
json_builder::add_string (const std::string &key, const std::string &value)
{
  append_key (key);
  append_escaped (value);
}

void
json_builder::add_optional_string (const std::string &key,
                                   const std::optional<std::string> &value)
{
  if (value) {
    add_string (key, *value);
  }
}

void
json_builder::add_int (const std::string &key, int64_t value)
{
  append_key (key);
  m_buffer.append (std::to_string (value));
}

void
json_builder::add_bool (const std::string &key, bool value)
{
  append_key (key);
  m_buffer.append (value ? "true" : "false");
}

void
json_builder::add_double (const std::string &key, double value)
{
  append_key (key);
  m_buffer.append (std::to_string (value));
}

void
json_builder::add_raw_json (const std::string &key, const std::string &raw_json)
{
  append_key (key);
  m_buffer.append (raw_json);
}

void
json_builder::add_array_string (const std::string &value)
{
  if (!m_first_in_scope) {
    m_buffer.push_back (',');
  }
  m_first_in_scope = false;
  append_escaped (value);
}

void
json_builder::add_array_int (int64_t value)
{
  if (!m_first_in_scope) {
    m_buffer.push_back (',');
  }
  m_first_in_scope = false;
  m_buffer.append (std::to_string (value));
}

void
json_builder::add_array_double (double value)
{
  if (!m_first_in_scope) {
    m_buffer.push_back (',');
  }
  m_first_in_scope = false;
  m_buffer.append (std::to_string (value));
}

void
json_builder::add_array_raw_json (const std::string &raw_json)
{
  if (!m_first_in_scope) {
    m_buffer.push_back (',');
  }
  m_first_in_scope = false;
  m_buffer.append (raw_json);
}

std::string
json_builder::str () const
{
  return m_buffer;
}

} // namespace agentsdk
