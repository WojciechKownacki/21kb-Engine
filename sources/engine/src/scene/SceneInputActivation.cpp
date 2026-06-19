#include "engine/scene/SceneInputActivation.hpp"

#include "engine/input/InputSubsystem.hpp"
#include "engine/ecs/Query.hpp"
#include "engine/ecs/UnsafeHotQuery.hpp"
#include "engine/scene/InputComponent.hpp"
#include "engine/scene/Scene.hpp"
#include "scene/SceneAccess.hpp"
#include "scene/SceneState.hpp"

#include <cstddef>

namespace kb::scene {

namespace {

template <typename Batch>
void ApplyInputMappingBatch(const Batch& batch, kb::input::InputSubsystem& input) {
    const InputComponent* components = batch.template Components<0>();
    for (std::size_t index = 0; index < batch.Count(); ++index) {
        const InputComponent& component = components[index];
        if (component.enabled && component.mappingContextAssetId != 0U) {
            static_cast<void>(input.AddMappingContext(component.mappingContextAssetId, component.priority));
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
    kb::ecs::UnsafeHotReadQuery<InputComponent> hotQuery;
    if (!hotQuery.Rebuild(query, settings)) {
        return;
    }
    hotQuery.ForEachRange(settings.maxBatchSize, [&input](const auto& batch) {
        ApplyInputMappingBatch(batch, input);
    });
}

void SceneInputActivation::Clear(Scene& scene) {
    scene.Input().ClearMappingContexts();
}

} // namespace kb::scene
