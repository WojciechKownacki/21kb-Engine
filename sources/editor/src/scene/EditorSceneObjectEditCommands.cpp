#include "scene/EditorSceneObjectEditCommands.hpp"

#include "engine/scene/SceneEntities.hpp"
#include "engine/scene/SceneHierarchyAccess.hpp"
#include "engine/scene/ScenePrefabs.hpp"
#include "engine/scene/SceneRuntime.hpp"
#include "engine/scene/SceneTransforms.hpp"
#include "scene/EditorSceneContext.hpp"

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

[[nodiscard]] bool SameVec3(kb::scene::Vec3 lhs, kb::scene::Vec3 rhs) noexcept {
    return lhs.x == rhs.x && lhs.y == rhs.y && lhs.z == rhs.z;
}

[[nodiscard]] bool SameQuat(kb::scene::Quat lhs, kb::scene::Quat rhs) noexcept {
    return lhs.x == rhs.x && lhs.y == rhs.y && lhs.z == rhs.z && lhs.w == rhs.w;
}

[[nodiscard]] bool SameTransform(const kb::scene::TransformComponent& lhs, const kb::scene::TransformComponent& rhs) noexcept {
    return SameVec3(lhs.localPosition, rhs.localPosition) &&
        SameQuat(lhs.localRotation, rhs.localRotation) &&
        SameVec3(lhs.localScale, rhs.localScale);
}

[[nodiscard]] kb::scene::SceneObject AliveParentObject(kb::scene::Scene& scene, kb::scene::SceneEntity parent) noexcept {
    if (!parent.IsValid() || !scene.Entities().IsAlive(parent)) {
        return {};
    }
    return scene.Entities().Object(parent);
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

        payloads.push_back(EditorSceneObjectPrefabPayload{
            .prefab = scene.Prefabs().Capture(object),
            .parent = scene.Hierarchy().Parent(entity),
        });
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
        if (SameTransform(current, target)) {
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
    for (const EditorSceneObjectPrefabPayload& payload : payloads_) {
        kb::scene::SceneObject parent = AliveParentObject(scene, payload.parent);
        const kb::scene::ScenePrefabInstance instance = scene.Prefabs().Instantiate(
            payload.prefab,
            kb::scene::ScenePrefabInstantiationSettings{ .parent = parent });
        if (!instance.Empty() && instance.RootObject().IsValid()) {
            currentEntities_.push_back(instance.RootObject().Entity());
        }
    }

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
