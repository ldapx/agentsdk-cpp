#pragma once

#include <concepts>
#include <optional>
#include <stdexcept>
#include <variant>

namespace agentsdk
{

/**
 * A simple result type for C++20.
 *
 * Holds either a value of type ``T`` or an error of type ``E``.
 * This is a subset of ``std::expected`` (C++23) for C++20 compatibility.
 *
 * Besides exact ``T``/``E`` arguments, a value convertible to exactly one
 * of the two alternatives is accepted implicitly, so a function returning
 * ``result<std::variant<task, message>, a2a_error>`` can
 * ``return message{...}`` directly.
 *
 * Shared by every protocol module (A2A, ACP, MCP) so that error handling is
 * uniform across the SDK.
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

  /**
   * Implicit conversion from a value convertible to exactly one of ``T``
   * or ``E``.  Participates only when the conversion is unambiguous, so a
   * type convertible to both alternatives (or neither) is rejected.
   */
  template <typename U>
    requires (!std::same_as<std::decay_t<U>, result>
              && std::constructible_from<T, U>
              && !std::constructible_from<E, U>)
  result (U &&value) // NOLINT implicit conversion
      : m_data (std::in_place_index<0>, std::forward<U> (value))
  {
  }

  /**
   * Implicit conversion from an error convertible to exactly one of ``T``
   * or ``E``.  See the value overload for the ambiguity rule.
   */
  template <typename U>
    requires (!std::same_as<std::decay_t<U>, result>
              && !std::constructible_from<T, U>
              && std::constructible_from<E, U>)
  result (U &&error) // NOLINT implicit conversion
      : m_data (std::in_place_index<1>, std::forward<U> (error))
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

  /** Member access. UB if the result holds an error. */
  const T *
  operator->() const
  {
    return &value ();
  }
  T *
  operator->()
  {
    return &value ();
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

} // namespace agentsdk
