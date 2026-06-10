#include "engine/scene/SceneInputActivation.hpp"

#include "engine/input/InputSubsystem.hpp"
#include "engine/scene/InputComponent.hpp"
#include "engine/scene/Scene.hpp"
#include "scene/SceneAccess.hpp"
#include "scene/SceneState.hpp"
#include "scene/components/SceneComponentIterationAccess.hpp"

#include <flecs.h>

#include <cstdint>

namespace kb::scene {

void SceneInputActivation::Apply(Scene& scene) {
    SceneState& state = SceneAccess::State(scene);
    kb::input::InputSubsystem& input = scene.Input();
    input.ClearMappingContexts();

    ecs_iter_t it = ecs_each_id(state.world.NativeHandle(), state.components.InputComponentId());
    while (ecs_each_next(&it)) {
        const auto* components = SceneComponentIterationAccess::Field<InputComponent>(it, 0);
        if (components == nullptr) {
            continue;
        }
        for (std::int32_t index = 0; index < it.count; ++index) {
            const InputComponent& component = components[index];
            if (component.enabled && component.mappingContextAssetId != 0U) {
                static_cast<void>(input.AddMappingContext(component.mappingContextAssetId, component.priority));
            }
        }
    }
}

void SceneInputActivation::Clear(Scene& scene) {
    scene.Input().ClearMappingContexts();
}

} // namespace kb::scene
