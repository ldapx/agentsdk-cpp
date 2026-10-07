#pragma once

// Forwarding header: the result type lives in the protocol-neutral
// `agentsdk` namespace (see <agentsdk/common/result.hpp>).  Spell it
// `agentsdk::result<T, E>`; unqualified `result` inside `agentsdk::a2a`
// resolves there too.  There is deliberately no `a2a::result` alias: it
// made `result` ambiguous for consumers using both `agentsdk` and
// `agentsdk::a2a` namespaces.

#include <agentsdk/common/result.hpp>
