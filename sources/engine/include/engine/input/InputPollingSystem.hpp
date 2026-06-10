#pragma once

#include "engine/scene/SceneSystem.hpp"

namespace kb::input {

class InputSubsystem;

// Runs in the scene's Input phase (before gameplay systems) and recomputes all
// action states from the current device snapshot each frame. Platform code is
// responsible for filling the subsystem's device state before Update runs.
class InputPollingSystem final : public kb::scene::SceneSystem {
public:
    explicit InputPollingSystem(InputSubsystem& subsystem) noexcept;

    void OnUpdate(kb::scene::SceneSystemContext& context) override;

private:
    InputSubsystem& subsystem_;
};

} // namespace kb::input
