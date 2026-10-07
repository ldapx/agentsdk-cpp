#pragma once

#include <atomic>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <agentsdk/a2a/transport.hpp>
#include <agentsdk/a2a/types.hpp>

namespace agentsdk::a2a
{

/**
 * Parses Server-Sent Events (SSE) from a byte stream.
 *
 * SSE format:
 *
 *   data: {"task": {...}}\n
 *   \n
 *
 * Call ``feed()`` with incoming data chunks, and the parser will invoke
 * the callback for each complete event.
 */
class sse_parser
{
public:
  using event_callback = std::function<void (const std::string &data)>;

  explicit sse_parser (event_callback callback);

  /**
   * Feed new data into the parser.
   *
   * :param data: Raw bytes from the SSE stream.
   * :param length: Number of bytes.
   */
  void feed (const char *data, size_t length);

  /** Signal that the stream has ended. */
  void finish ();

private:
  event_callback m_callback;
  std::string m_buffer;
};

/**
 * Concrete stream handle for background streaming.
 */
class background_stream_handle : public stream_handle
{
public:
  background_stream_handle ();
  ~background_stream_handle () override;

  void cancel () override;
  bool is_active () const override;

  /** Set the worker thread (owned by this handle). */
  void set_thread (std::thread thread);

private:
  std::atomic<bool> m_active{ false };
  std::thread m_thread;
};

/**
 * Simple in-process stream for testing or local use.
 *
 * Pushes events from the server side; the observer receives them.
 */
class local_stream : public stream_handle
{
public:
  explicit local_stream (stream_observer &observer);

  void cancel () override;
  bool is_active () const override;

  /** Push an event to the observer. Thread-safe. */
  void push_event (const stream_response &event);

  /** Signal stream completion. */
  void complete ();

  /** Signal stream error. */
  void error (const a2a_error &err);

private:
  stream_observer &m_observer;
  std::atomic<bool> m_active{ true };
};

} // namespace agentsdk::a2a
