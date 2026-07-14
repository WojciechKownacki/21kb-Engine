#pragma once

#include "engine/scene/SceneSystem.hpp"

namespace kb::input {

// Runs in the scene's Input phase (before gameplay systems) and recomputes all
// action states from the current device snapshot each frame, for the primary
// local user AND every other local user created via Scene::Input(LocalUserId)
// (LIB-115) - all reading the primary's shared physical device state. Platform
// code is responsible for filling the primary subsystem's device state before
// Update runs.
class InputPollingSystem final : public kb::scene::SceneSystem {
public:
    void OnUpdate(kb::scene::SceneSystemContext& context) override;
};

} // namespace kb::input
