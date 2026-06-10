#include "engine/input/InputPollingSystem.hpp"

#include "engine/input/InputSubsystem.hpp"
#include "engine/scene/SceneSystemContext.hpp"

namespace kb::input {

InputPollingSystem::InputPollingSystem(InputSubsystem& subsystem) noexcept
    : subsystem_(subsystem) {}

void InputPollingSystem::OnUpdate(kb::scene::SceneSystemContext& context) {
    subsystem_.Evaluate(context.DeltaSeconds());
}

} // namespace kb::input
