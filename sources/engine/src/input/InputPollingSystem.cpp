#include "engine/input/InputPollingSystem.hpp"

#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneSystemContext.hpp"

namespace kb::input {

void InputPollingSystem::OnUpdate(kb::scene::SceneSystemContext& context) {
    context.GetScene().EvaluateAllLocalUserInput(context.DeltaSeconds());
}

} // namespace kb::input
