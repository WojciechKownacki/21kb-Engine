#include "scene/SceneAccess.hpp"
#include "scene/SceneComponentMutationService.hpp"
#include "scene/SceneComponentQueryService.hpp"
#include "scene/SceneEntityService.hpp"
#include "scene/SceneHistoryService.hpp"
#include "scene/SceneState.hpp"
#include "scene/prefab/ScenePrefabDirtyTracker.hpp"

namespace kb::scene {

bool SceneComponentQueryService::HasBehaviour(const Scene& scene, SceneEntity entity) noexcept {
    return SceneEntityService::IsAlive(scene, entity) && SceneAccess::State(scene).componentStorage.Behaviours().Has(entity);
}

const BehaviourComponent* SceneComponentQueryService::TryGetBehaviour(const Scene& scene, SceneEntity entity) noexcept {
    return SceneEntityService::IsAlive(scene, entity) ? SceneAccess::State(scene).componentStorage.Behaviours().TryGet(entity) : nullptr;
}

std::size_t SceneComponentQueryService::BehaviourCount(const Scene& scene, SceneEntity entity) noexcept {
    return SceneEntityService::IsAlive(scene, entity) ? SceneAccess::State(scene).componentStorage.Behaviours().Count(entity) : 0U;
}

const BehaviourComponent* SceneComponentQueryService::TryGetBehaviourAt(const Scene& scene, SceneEntity entity, std::size_t index) noexcept {
    return SceneEntityService::IsAlive(scene, entity) ? SceneAccess::State(scene).componentStorage.Behaviours().TryGetAt(entity, index) : nullptr;
}

const BehaviourComponent* SceneComponentQueryService::FindBehaviour(
    const Scene& scene, SceneEntity entity, std::uint64_t assetId, BehaviourBackend backend) noexcept {
    if (!SceneEntityService::IsAlive(scene, entity)) {
        return nullptr;
    }
    const SceneBehaviourComponentStore& store = SceneAccess::State(scene).componentStorage.Behaviours();
    const std::size_t index = store.IndexOf(entity, assetId, backend);
    return index == SceneBehaviourComponentStore::kNoBehaviour ? nullptr : store.TryGetAt(entity, index);
}

std::span<const BehaviourComponent> SceneComponentQueryService::AdditionalBehaviours(const Scene& scene, SceneEntity entity) noexcept {
    return SceneEntityService::IsAlive(scene, entity) ? SceneAccess::State(scene).componentStorage.Behaviours().Additional(entity)
                                                       : std::span<const BehaviourComponent>{};
}

const std::uint64_t* SceneComponentQueryService::BehaviourRevisionCounter(const Scene& scene) noexcept {
    return SceneAccess::State(scene).componentStorage.Behaviours().RevisionCounter();
}

std::uint64_t SceneComponentQueryService::BehaviourChangeStamp(const Scene& scene) noexcept {
    return SceneAccess::State(scene).componentStorage.Behaviours().ChangeStamp();
}

BehaviourComponent* SceneComponentMutationService::TryGetBehaviour(Scene& scene, SceneEntity entity) noexcept {
    SceneHistoryService::NoteObjectChanging(scene, entity);
    return SceneEntityService::IsAlive(scene, entity) ? SceneAccess::State(scene).componentStorage.Behaviours().TryGet(entity) : nullptr;
}

void SceneComponentMutationService::SetBehaviour(Scene& scene, SceneEntity entity, const BehaviourComponent& behaviour) {
    SceneHistoryService::NoteObjectChanging(scene, entity);
    if (SceneEntityService::IsAlive(scene, entity)) {
        SceneState& state = SceneAccess::State(scene);
        state.componentStorage.Behaviours().Set(entity, behaviour);
        MarkScenePrefabNodeDirty(state, entity);
    }
}

void SceneComponentMutationService::RemoveBehaviour(Scene& scene, SceneEntity entity) noexcept {
    SceneHistoryService::NoteObjectChanging(scene, entity);
    if (SceneEntityService::IsAlive(scene, entity)) {
        SceneState& state = SceneAccess::State(scene);
        state.componentStorage.Behaviours().Remove(entity);
        MarkScenePrefabNodeDirty(state, entity);
    }
}

bool SceneComponentMutationService::AddBehaviour(Scene& scene, SceneEntity entity, const BehaviourComponent& behaviour) {
    SceneHistoryService::NoteObjectChanging(scene, entity);
    if (!SceneEntityService::IsAlive(scene, entity)) {
        return false;
    }
    SceneState& state = SceneAccess::State(scene);
    const bool added = state.componentStorage.Behaviours().Add(entity, behaviour);
    if (added) {
        MarkScenePrefabNodeDirty(state, entity);
    }
    return added;
}

bool SceneComponentMutationService::RemoveBehaviourAt(Scene& scene, SceneEntity entity, std::size_t index) noexcept {
    SceneHistoryService::NoteObjectChanging(scene, entity);
    if (!SceneEntityService::IsAlive(scene, entity)) {
        return false;
    }
    SceneState& state = SceneAccess::State(scene);
    const bool removed = state.componentStorage.Behaviours().RemoveAt(entity, index);
    if (removed) {
        MarkScenePrefabNodeDirty(state, entity);
    }
    return removed;
}

bool SceneComponentMutationService::SetBehaviourAt(Scene& scene, SceneEntity entity, std::size_t index, const BehaviourComponent& behaviour) {
    SceneHistoryService::NoteObjectChanging(scene, entity);
    if (!SceneEntityService::IsAlive(scene, entity)) {
        return false;
    }
    SceneState& state = SceneAccess::State(scene);
    const bool written = state.componentStorage.Behaviours().SetAt(entity, index, behaviour);
    if (written) {
        MarkScenePrefabNodeDirty(state, entity);
    }
    return written;
}

void SceneComponentMutationService::SetAdditionalBehaviours(Scene& scene, SceneEntity entity, std::span<const BehaviourComponent> additional) {
    SceneHistoryService::NoteObjectChanging(scene, entity);
    if (SceneEntityService::IsAlive(scene, entity)) {
        SceneState& state = SceneAccess::State(scene);
        state.componentStorage.Behaviours().SetAdditional(entity, additional);
        MarkScenePrefabNodeDirty(state, entity);
    }
}

void SceneComponentMutationService::MarkBehaviourModified(Scene& scene, SceneEntity entity) noexcept {
    if (SceneEntityService::IsAlive(scene, entity)) {
        SceneState& state = SceneAccess::State(scene);
        state.componentStorage.Behaviours().MarkModified(entity);
        MarkScenePrefabNodeDirty(state, entity);
    }
}

} // namespace kb::scene
