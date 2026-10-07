#pragma once

#include <optional>
#include <stdexcept>
#include <variant>

namespace agentsdk::a2a
{

/**
 * A simple result type for C++20.
 *
 * Holds either a value of type ``T`` or an error of type ``E``.
 * This is a subset of ``std::expected`` (C++23) for C++20 compatibility.
 */
template <typename T, typename E> class result
{
public:
  result (const T &value) // NOLINT implicit conversion
      : m_data (value)
  {
  }

  result (T &&value) // NOLINT implicit conversion
      : m_data (std::move (value))
  {
  }

  result (const E &error) // NOLINT implicit conversion
      : m_data (error)
  {
  }

  result (E &&error) // NOLINT implicit conversion
      : m_data (std::move (error))
  {
  }

  /** Returns ``true`` if the result holds a value. */
  bool
  has_value () const
  {
    return std::holds_alternative<T> (m_data);
  }

  /** Returns ``true`` if the result holds an error. */
  bool
  has_error () const
  {
    return std::holds_alternative<E> (m_data);
  }

  /** Access the value. UB if the result holds an error. */
  const T &
  value () const &
  {
    return std::get<T> (m_data);
  }

  T &
  value () &
  {
    return std::get<T> (m_data);
  }

  T &&
  value () &&
  {
    return std::get<T> (std::move (m_data));
  }

  /** Access the error. UB if the result holds a value. */
  const E &
  error () const &
  {
    return std::get<E> (m_data);
  }

  E &
  error () &
  {
    return std::get<E> (m_data);
  }

  E &&
  error () &&
  {
    return std::get<E> (std::move (m_data));
  }

  /** Dereference operator. UB if the result holds an error. */
  const T &
  operator* () const &
  {
    return value ();
  }
  T &
  operator* () &
  {
    return value ();
  }
  T &&
  operator* () &&
  {
    return std::move (*this).value ();
  }

  /** Bool conversion. Returns ``true`` if the result holds a value. */
  explicit
  operator bool () const
  {
    return has_value ();
  }

private:
  std::variant<T, E> m_data;
};

/**
 * Specialization for ``result<void, E>``.
 *
 * Holds either success (no data) or an error.
 */
template <typename E> class result<void, E>
{
public:
  result () = default;

  result (const E &error) // NOLINT implicit conversion
      : m_error (error)
  {
  }

  result (E &&error) // NOLINT implicit conversion
      : m_error (std::move (error))
  {
  }

  bool
  has_value () const
  {
    return !m_error.has_value ();
  }
  bool
  has_error () const
  {
    return m_error.has_value ();
  }

  const E &
  error () const &
  {
    return *m_error;
  }
  E &
  error () &
  {
    return *m_error;
  }
  E &&
  error () &&
  {
    return std::move (*m_error);
  }

  explicit
  operator bool () const
  {
    return has_value ();
  }

private:
  std::optional<E> m_error;
};

} // namespace agentsdk::a2a
