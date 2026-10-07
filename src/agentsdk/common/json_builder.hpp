#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace agentsdk
{

/**
 * Lightweight JSON object builder.
 *
 * Produces compact JSON strings for serializing protocol types.
 * Manages nesting via an internal scope stack.
 *
 * Shared by every protocol module (A2A, ACP, MCP) so that request and
 * notification payloads are built the same way everywhere.
 */
class json_builder
{
public:
  json_builder ();

  json_builder (const json_builder &) = delete;
  json_builder &operator= (const json_builder &) = delete;

  /** Begin a new JSON object. */
  void begin_object ();
  /** Begin a new JSON object with a key (for nesting). */
  void begin_object (const std::string &key);
  /** End the current JSON object. */
  void end_object ();

  /** Begin a new JSON array. */
  void begin_array ();
  /** Begin a new JSON array with a key (for nesting). */
  void begin_array (const std::string &key);
  /** End the current JSON array. */
  void end_array ();

  /** Add a key-value pair with a string value. */
  void add_string (const std::string &key, const std::string &value);
  /** Add an optional string value (omitted if empty). */
  void add_optional_string (const std::string &key,
                            const std::optional<std::string> &value);
  /** Add a key-value pair with an integer value. */
  void add_int (const std::string &key, int64_t value);
  /** Add a key-value pair with a boolean value. */
  void add_bool (const std::string &key, bool value);
  /** Add a key-value pair with a floating-point value. */
  void add_double (const std::string &key, double value);
  /** Add a raw JSON string as a value. */
  void add_raw_json (const std::string &key, const std::string &raw_json);

  /** Add a string element to the current array. */
  void add_array_string (const std::string &value);
  /** Add an integer element to the current array. */
  void add_array_int (int64_t value);
  /** Add a floating-point element to the current array. */
  void add_array_double (double value);
  /** Add a raw JSON element to the current array. */
  void add_array_raw_json (const std::string &raw_json);

  /** Get the accumulated JSON string. */
  std::string str () const;

  /**
   * Escape a string for use as a JSON string body (without quotes).
   */
  static std::string escape (const std::string &value);

private:
  void append_key (const std::string &key);
  void append_escaped (const std::string &value);

  std::string m_buffer;
  /** Tracks scope types: ``true`` = object, ``false`` = array. */
  std::vector<bool> m_scopes;
  bool m_first_in_scope = true;
};

} // namespace agentsdk
