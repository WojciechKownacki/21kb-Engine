#include "engine/scene/SceneInputActivation.hpp"

#include "engine/input/InputSubsystem.hpp"
#include "engine/ecs/Query.hpp"
#include "engine/scene/InputComponent.hpp"
#include "engine/scene/Scene.hpp"
#include "scene/SceneAccess.hpp"
#include "scene/SceneState.hpp"

#include <cstddef>

namespace kb::scene {

namespace {

void ApplyInputMappingBatch(const kb::ecs::QueryBatch<InputComponent>& batch, void* rawContext) {
    auto* input = static_cast<kb::input::InputSubsystem*>(rawContext);
    const InputComponent* components = batch.Components<0>();
    for (std::size_t index = 0; index < batch.Count(); ++index) {
        const InputComponent& component = components[index];
        if (component.enabled && component.mappingContextAssetId != 0U) {
            static_cast<void>(input->AddMappingContext(component.mappingContextAssetId, component.priority));
        }
    }
}

} // namespace

void SceneInputActivation::Apply(Scene& scene) {
    SceneState& state = SceneAccess::State(scene);
    kb::input::InputSubsystem& input = scene.Input();
    input.ClearMappingContexts();

    kb::ecs::Query<InputComponent> query = state.world.CreateQuery<InputComponent>();
    if (!query.IsValid()) {
        return;
    }

    kb::ecs::QueryExecutionSettings settings;
    settings.policy = kb::ecs::QueryExecutionPolicy::SingleThread;
    query.ForEachBatch(settings, ApplyInputMappingBatch, &input);
}

void SceneInputActivation::Clear(Scene& scene) {
    scene.Input().ClearMappingContexts();
}

} // namespace kb::scene
