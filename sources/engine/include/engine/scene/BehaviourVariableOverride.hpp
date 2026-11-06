#pragma once

#include "engine/script/ScriptValue.hpp"

#include <algorithm>
#include <span>
#include <string>

namespace kb::scene {

// A per-instance override of a behaviour's exposed ("@expose") script variable,
// authored in the editor Inspector. Following the standard scene-authoring design
// (delta-over-default serialization), only variables whose value DIFFERS from the
// script's declared default are stored; a variable reset to its default drops its
// override entirely. `name` is the exposed variable identifier; `value` is the
// authored value, marshalled through the same ScriptValue that crosses the script
// boundary so no second value type or conversion layer is introduced.
struct BehaviourVariableOverride {
    std::string name;
    kb::script::ScriptValue value;
};

// Two override sets are the same when they hold the same variables with the same values, in any order.
[[nodiscard]] inline bool SameBehaviourVariableOverrides(
    std::span<const BehaviourVariableOverride> lhs, std::span<const BehaviourVariableOverride> rhs) noexcept {
    return lhs.size() == rhs.size() && std::ranges::all_of(lhs, [rhs](const BehaviourVariableOverride& entry) noexcept {
        return std::ranges::any_of(rhs, [&entry](const BehaviourVariableOverride& other) noexcept {
            return other.name == entry.name && other.value == entry.value;
        });
    });
}

} // namespace kb::scene
