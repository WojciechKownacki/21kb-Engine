#include "scene/EditorSceneObjectEditCommands.hpp"

#include "engine/scene/SceneComponents.hpp"
#include "engine/scene/SceneEntities.hpp"
#include "engine/scene/SceneHierarchyAccess.hpp"
#include "engine/scene/ScenePrefabs.hpp"
#include "engine/scene/SceneRuntime.hpp"
#include "engine/scene/SceneTransforms.hpp"
#include "engine/scene/SceneUIComponentSet.hpp"
#include "scene/EditorSceneContext.hpp"
#include "scene/transform_edit/EditorSceneTransformEquality.hpp"

#include <unordered_map>
#include <algorithm>
#include <utility>

namespace kb::editor {
namespace {

[[nodiscard]] bool AnyAlive(kb::scene::Scene& scene, std::span<const kb::scene::SceneEntity> entities) noexcept {
    for (const kb::scene::SceneEntity entity : entities) {
        if (scene.Entities().IsAlive(entity)) {
            return true;
        }
    }
    return false;
}

[[nodiscard]] kb::scene::SceneObject AliveParentObject(kb::scene::Scene& scene, kb::scene::SceneEntity parent) noexcept {
    if (!parent.IsValid() || !scene.Entities().IsAlive(parent)) {
        return {};
    }
    return scene.Entities().Object(parent);
}

// Objects outside a restored subtree may link to it by entity id. Those ids died with the deleted
// objects; point every such UI navigation link at the object that now stands in its place.
void RelinkNavigation(kb::scene::Scene& scene, const std::unordered_map<std::uint64_t, std::uint64_t>& restoredIds) {
    if (restoredIds.empty()) {
        return;
    }
    kb::scene::SceneUIComponents ui = scene.Components().UI();
    std::vector<kb::scene::SceneEntity> pending = scene.Hierarchy().RootEntities();
    while (!pending.empty()) {
        const kb::scene::SceneEntity entity = pending.back();
        pending.pop_back();
        for (std::size_t index = 0U; index < scene.Hierarchy().ChildCount(entity); ++index) {
            pending.push_back(scene.Hierarchy().ChildAt(entity, index));
        }
        kb::scene::UISelectable* selectable = ui.TryGet<kb::scene::UISelectable>(entity);
        if (selectable == nullptr) {
            continue;
        }
        bool changed = false;
        for (std::uint64_t* link : { &selectable->navigationUp, &selectable->navigationDown,
                 &selectable->navigationLeft, &selectable->navigationRight }) {
            const auto restored = restoredIds.find(*link);
            if (*link != 0U && restored != restoredIds.end()) {
                *link = restored->second;
                changed = true;
            }
        }
        if (changed) {
            ui.MarkModified<kb::scene::UISelectable>(entity);
        }
    }
}

} // namespace

std::vector<EditorSceneObjectPrefabPayload> EditorSceneObjectPayloadBuilder::Capture(
    EditorSceneContext& context,
    std::span<const kb::scene::SceneEntity> entities) {
    kb::scene::Scene& scene = context.Scene();
    std::vector<EditorSceneObjectPrefabPayload> payloads;
    payloads.reserve(entities.size());

    for (const kb::scene::SceneEntity entity : entities) {
        if (!scene.Entities().IsAlive(entity)) {
            continue;
        }

        kb::scene::SceneObject object = scene.Entities().Object(entity);
        if (!object.IsValid()) {
            continue;
        }

        EditorSceneObjectPrefabPayload payload{
            .prefab = scene.Prefabs().Capture(object),
            .parent = scene.Hierarchy().Parent(entity),
        };
        // Capture walks the subtree depth-first in child order; the same walk gives node order.
        std::vector<kb::scene::SceneEntity> pending{ entity };
        while (!pending.empty()) {
            const kb::scene::SceneEntity current = pending.back();
            pending.pop_back();
            payload.capturedEntities.push_back(current);
            for (std::size_t index = scene.Hierarchy().ChildCount(current); index > 0U; --index) {
                pending.push_back(scene.Hierarchy().ChildAt(current, index - 1U));
            }
        }
        if (payload.capturedEntities.size() != payload.prefab.NodeCount()) {
            payload.capturedEntities.clear();
        }
        payloads.push_back(std::move(payload));
    }
    return payloads;
}

EditorSceneTransformDeltaCommand::EditorSceneTransformDeltaCommand(
    EditorSceneContext& context,
    std::string label,
    std::vector<EditorSceneObjectTransformChange> changes)
    : context_(context)
    , label_(std::move(label))
    , changes_(std::move(changes)) {}

std::string_view EditorSceneTransformDeltaCommand::Label() const noexcept {
    return label_;
}

bool EditorSceneTransformDeltaCommand::Execute() {
    return Apply(true);
}

bool EditorSceneTransformDeltaCommand::Undo() {
    return Apply(false);
}

bool EditorSceneTransformDeltaCommand::Redo() {
    return Apply(true);
}

bool EditorSceneTransformDeltaCommand::Apply(bool after) {
    bool changed = false;
    std::vector<kb::scene::SceneEntity> touched;
    touched.reserve(changes_.size());
    kb::scene::Scene& scene = context_.Scene();
    for (const EditorSceneObjectTransformChange& change : changes_) {
        if (!scene.Entities().IsAlive(change.entity)) {
            continue;
        }

        const kb::scene::TransformComponent& target = after ? change.after : change.before;
        const kb::scene::TransformComponent current = scene.Transforms().Get(change.entity);
        if (EditorSceneTransformEquality::Same(current, target)) {
            continue;
        }

        scene.Transforms().Set(change.entity, target);
        touched.push_back(change.entity);
        changed = true;
    }

    if (changed) {
        context_.MarkSceneEntitiesRenderDirty(touched);
        scene.Runtime().SynchronizeTransforms();
    }
    return changed;
}

EditorScenePrefabSpawnCommand::EditorScenePrefabSpawnCommand(
    EditorSceneContext& context,
    std::string label,
    std::vector<EditorSceneObjectPrefabPayload> payloads)
    : context_(context)
    , label_(std::move(label))
    , payloads_(std::move(payloads)) {}

EditorScenePrefabSpawnCommand::EditorScenePrefabSpawnCommand(
    EditorSceneContext& context,
    std::string label,
    std::vector<EditorSceneObjectPrefabPayload> payloads,
    std::vector<kb::scene::SceneEntity> materializedRoots)
    : context_(context)
    , label_(std::move(label))
    , payloads_(std::move(payloads))
    , createdEntities_(std::move(materializedRoots))
    , materializedOnConstruction_(true) {}

std::string_view EditorScenePrefabSpawnCommand::Label() const noexcept {
    return label_;
}

bool EditorScenePrefabSpawnCommand::Execute() {
    if (materializedOnConstruction_) {
        materializedOnConstruction_ = false;
        if (!AnyAlive(context_.Scene(), createdEntities_)) {
            return false;
        }
        NotifyChanged();
        SelectCreatedOrClear();
        return true;
    }
    return InstantiatePayloads();
}

bool EditorScenePrefabSpawnCommand::Undo() {
    const bool destroyed = DestroyCreated();
    if (destroyed) {
        context_.ClearHierarchySelection();
        NotifyChanged();
    }
    return destroyed;
}

bool EditorScenePrefabSpawnCommand::Redo() {
    return InstantiatePayloads();
}

const std::vector<kb::scene::SceneEntity>& EditorScenePrefabSpawnCommand::CreatedEntities() const noexcept {
    return createdEntities_;
}

bool EditorScenePrefabSpawnCommand::InstantiatePayloads() {
    if (payloads_.empty()) {
        return false;
    }

    kb::scene::Scene& scene = context_.Scene();
    createdEntities_.clear();
    createdEntities_.reserve(payloads_.size());
    for (const EditorSceneObjectPrefabPayload& payload : payloads_) {
        kb::scene::SceneObject parent = AliveParentObject(scene, payload.parent);
        const kb::scene::ScenePrefabInstance instance = scene.Prefabs().Instantiate(
            payload.prefab,
            kb::scene::ScenePrefabInstantiationSettings{ .parent = parent });
        if (!instance.Empty() && instance.RootObject().IsValid()) {
            createdEntities_.push_back(instance.RootObject().Entity());
        }
    }

    if (createdEntities_.empty()) {
        return false;
    }

    NotifyChanged();
    SelectCreatedOrClear();
    return true;
}

bool EditorScenePrefabSpawnCommand::DestroyCreated() {
    kb::scene::Scene& scene = context_.Scene();
    bool destroyed = false;
    for (const kb::scene::SceneEntity entity : createdEntities_) {
        if (!scene.Entities().IsAlive(entity)) {
            continue;
        }
        scene.Entities().Destroy(entity);
        destroyed = true;
    }
    return destroyed;
}

void EditorScenePrefabSpawnCommand::NotifyChanged() {
    context_.MarkSceneRenderDirty();
    context_.Scene().Runtime().SynchronizeTransforms();
}

void EditorScenePrefabSpawnCommand::SelectCreatedOrClear() {
    if (createdEntities_.empty()) {
        context_.ClearHierarchySelection();
        return;
    }
    context_.SelectHierarchyEntities(createdEntities_);
}

EditorScenePrefabRemoveCommand::EditorScenePrefabRemoveCommand(
    EditorSceneContext& context,
    std::string label,
    std::vector<kb::scene::SceneEntity> entities,
    std::vector<EditorSceneObjectPrefabPayload> payloads)
    : context_(context)
    , label_(std::move(label))
    , currentEntities_(std::move(entities))
    , payloads_(std::move(payloads)) {}

std::string_view EditorScenePrefabRemoveCommand::Label() const noexcept {
    return label_;
}

bool EditorScenePrefabRemoveCommand::Execute() {
    return DestroyCurrent();
}

bool EditorScenePrefabRemoveCommand::Undo() {
    return RestorePayloads();
}

bool EditorScenePrefabRemoveCommand::Redo() {
    return DestroyCurrent();
}

bool EditorScenePrefabRemoveCommand::DestroyCurrent() {
    kb::scene::Scene& scene = context_.Scene();
    bool destroyed = false;
    for (const kb::scene::SceneEntity entity : currentEntities_) {
        if (!scene.Entities().IsAlive(entity)) {
            continue;
        }
        scene.Entities().Destroy(entity);
        destroyed = true;
    }

    if (destroyed) {
        context_.ClearHierarchySelection();
        NotifyChanged();
    }
    return destroyed;
}

bool EditorScenePrefabRemoveCommand::RestorePayloads() {
    if (payloads_.empty()) {
        return false;
    }

    kb::scene::Scene& scene = context_.Scene();
    currentEntities_.clear();
    currentEntities_.reserve(payloads_.size());
    std::unordered_map<std::uint64_t, std::uint64_t> restoredIds;
    for (EditorSceneObjectPrefabPayload& payload : payloads_) {
        kb::scene::SceneObject parent = AliveParentObject(scene, payload.parent);
        const kb::scene::ScenePrefabInstance instance = scene.Prefabs().Instantiate(
            payload.prefab,
            kb::scene::ScenePrefabInstantiationSettings{ .parent = parent });
        if (!instance.Empty() && instance.RootObject().IsValid()) {
            currentEntities_.push_back(instance.RootObject().Entity());
        }
        if (instance.ObjectCount() == payload.capturedEntities.size()) {
            for (std::size_t index = 0U; index < payload.capturedEntities.size(); ++index) {
                const kb::scene::SceneEntity restored = instance.ObjectAt(static_cast<std::uint32_t>(index)).Entity();
                restoredIds.emplace(payload.capturedEntities[index].Id(), restored.Id());
                payload.capturedEntities[index] = restored;
            }
        }
    }
    RelinkNavigation(scene, restoredIds);

    if (currentEntities_.empty()) {
        return false;
    }

    context_.SelectHierarchyEntities(currentEntities_);
    NotifyChanged();
    return true;
}

void EditorScenePrefabRemoveCommand::NotifyChanged() {
    context_.MarkSceneRenderDirty();
    context_.Scene().Runtime().SynchronizeTransforms();
}

} // namespace kb::editor
