#pragma once

#include "engine/input/InputActionValueType.hpp"

#include <string>

namespace kb::input {

// An InputAction is an abstract, named thing the player can do ("Jump", "Move").
// Mapping contexts bind physical keys to actions; scripts query actions by name.
// A lightweight, data-only asset - identity and value shape only.
struct InputActionAsset {
    std::string name;
    InputActionValueType valueType = InputActionValueType::Bool;
    // When true, a higher-priority context consuming this action blocks lower
    // contexts from also receiving the underlying keys.
    bool consumeInput = true;
};

} // namespace kb::input
