// Part of agentsdk_core_tests; the doctest main lives in
// test_main.cpp.
#include "doctest.h"

#include <agentsdk/common/result.hpp>

#include <string>
#include <variant>

using namespace agentsdk;

namespace
{

struct widget
{
  std::string name;
  int size = 0;
};

struct wire_error
{
  int64_t code = 0;
  std::string message;
};

} // namespace

TEST_CASE ("result: holds exact values and errors")
{
  result<int, std::string> ok{ 42 };
  CHECK (ok.has_value ());
  CHECK (!ok.has_error ());
  CHECK (static_cast<bool> (ok));
  CHECK (ok.value () == 42);
  CHECK (*ok == 42);

  result<int, std::string> fail{ std::string{ "boom" } };
  CHECK (!fail.has_value ());
  CHECK (fail.has_error ());
  CHECK (!static_cast<bool> (fail));
  CHECK (fail.error () == "boom");
}

TEST_CASE ("result: member access through operator->")
{
  result<widget, wire_error> ok{ widget{ "gear", 7 } };
  REQUIRE (ok.has_value ());
  CHECK (ok->name == "gear");
  CHECK (ok->size == 7);

  const result<widget, wire_error> &cref = ok;
  CHECK (cref->name == "gear");
}

TEST_CASE ("result: implicit conversion to one variant alternative")
{
  using response = std::variant<widget, int>;

  // widget is convertible to the variant but not to the error.
  result<response, wire_error> ok{ widget{ "gear", 7 } };
  REQUIRE (ok.has_value ());
  const widget *w = std::get_if<widget> (&ok.value ());
  REQUIRE (w != nullptr);
  CHECK (w->name == "gear");

  // int is convertible to the variant but not to the error.
  result<response, wire_error> count{ 3 };
  REQUIRE (count.has_value ());
  CHECK (std::get<int> (count.value ()) == 3);

  // wire_error converts to the error but not to the variant.
  result<response, wire_error> fail{ wire_error{ -1, "nope" } };
  REQUIRE (!fail.has_value ());
  CHECK (fail.error ().code == -1);
}

TEST_CASE ("result<void>: success default-constructs, error carries payload")
{
  result<void, wire_error> ok;
  CHECK (ok.has_value ());
  CHECK (static_cast<bool> (ok));

  result<void, wire_error> fail{ wire_error{ -32000, "timed out" } };
  CHECK (!fail.has_value ());
  CHECK (fail.error ().message == "timed out");
}
