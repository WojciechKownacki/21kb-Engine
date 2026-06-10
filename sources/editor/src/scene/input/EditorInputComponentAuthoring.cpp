#include "scene/input/EditorInputComponentAuthoring.hpp"

#include "console/EditorConsoleState.hpp"
#include "engine/scene/InputComponent.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneComponents.hpp"
#include "engine/scene/SceneEntities.hpp"
#include "engine/scene/SceneInputComponents.hpp"

#include <utility>
#include <vector>

namespace kb::editor {

EditorInputComponentAuthoring::EditorInputComponentAuthoring(kb::scene::Scene& scene, EditorConsoleState& console, CommandRunner runCommand, SelectEntityFn selectEntity) noexcept
    : scene_(scene)
    , console_(console)
    , catalog_(scene)
    , runCommand_(std::move(runCommand))
    , selectEntity_(std::move(selectEntity)) {}

bool EditorInputComponentAuthoring::Add(kb::scene::SceneEntity entity) {
    if (!scene_.Entities().IsAlive(entity) || scene_.Components().Inputs().Has(entity)) {
        return false;
    }
    const bool added = runCommand_("Add Input Component", [this, entity]() {
        scene_.Components().Inputs().Set(entity, kb::scene::InputComponent{});
        return true;
    });
    if (added) {
        selectEntity_(entity);
        console_.Info("Input", "Input component added.");
    }
    return added;
}

bool EditorInputComponentAuthoring::Remove(kb::scene::SceneEntity entity) {
    if (!scene_.Components().Inputs().Has(entity)) {
        return false;
    }
    return runCommand_("Remove Input Component", [this, entity]() {
        scene_.Components().Inputs().Remove(entity);
        return true;
    });
}

bool EditorInputComponentAuthoring::ToggleEnabled(kb::scene::SceneEntity entity) {
    return ApplyEdit(entity, "Toggle Input Enabled", [](kb::scene::InputComponent& component) {
        component.enabled = !component.enabled;
    });
}

bool EditorInputComponentAuthoring::SetPriority(kb::scene::SceneEntity entity, std::int32_t priority) {
    return ApplyEdit(entity, "Edit Input Priority", [priority](kb::scene::InputComponent& component) {
        component.priority = priority;
    });
}

bool EditorInputComponentAuthoring::CycleMappingContext(kb::scene::SceneEntity entity) {
    const std::vector<std::uint64_t> contexts = catalog_.SortedIdsOfType("InputMappingContext");
    if (contexts.empty()) {
        console_.Warning("Input", "No mapping context assets exist to assign.");
        return false;
    }
    return ApplyEdit(entity, "Assign Mapping Context", [&contexts](kb::scene::InputComponent& component) {
        component.mappingContextAssetId = EditorInputAssetCatalog::NextCyclicId(contexts, component.mappingContextAssetId);
    });
}

bool EditorInputComponentAuthoring::ApplyEdit(kb::scene::SceneEntity entity, std::string label, const std::function<void(kb::scene::InputComponent&)>& edit) {
    const kb::scene::InputComponent* current = scene_.Components().Inputs().TryGet(entity);
    if (current == nullptr) {
        return false;
    }
    kb::scene::InputComponent updated = *current;
    edit(updated);
    return runCommand_(std::move(label), [this, entity, updated]() {
        scene_.Components().Inputs().Set(entity, updated);
        return true;
    });
}

} // namespace kb::editor
