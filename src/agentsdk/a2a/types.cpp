#include <agentsdk/a2a/types.hpp>

namespace agentsdk::a2a
{

bool
is_terminal (task_state state)
{
  switch (state) {
  case task_state::completed:
  case task_state::failed:
  case task_state::canceled:
  case task_state::rejected:
    return true;
  default:
    return false;
  }
}

bool
is_interrupted (task_state state)
{
  switch (state) {
  case task_state::input_required:
  case task_state::auth_required:
    return true;
  default:
    return false;
  }
}

} // namespace agentsdk::a2a
