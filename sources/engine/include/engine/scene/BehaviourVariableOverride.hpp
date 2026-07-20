#pragma once

#include "engine/script/ScriptValue.hpp"

#include <string>

namespace kb::scene {

// A per-instance override of a behaviour's exposed ("@expose") script variable,
// authored in the editor Inspector. Following the Unity/Unreal/Godot/O3DE design
// (delta-over-default serialization), only variables whose value DIFFERS from the
// script's declared default are stored; a variable reset to its default drops its
// override entirely. `name` is the exposed variable identifier; `value` is the
// authored value, marshalled through the same ScriptValue that crosses the script
// boundary so no second value type or conversion layer is introduced.
struct BehaviourVariableOverride {
    std::string name;
    kb::script::ScriptValue value;
};

} // namespace kb::scene
