#include "engine/scene/SceneComponents.hpp"

#include "scene/SceneComponentMutationService.hpp"
#include "scene/SceneComponentQueryService.hpp"
#include "scene/SceneIterationService.hpp"

namespace kb::scene {

SceneBehaviourComponentQueries::SceneBehaviourComponentQueries(const Scene& scene) noexcept
    : scene_(scene) {}

bool SceneBehaviourComponentQueries::Has(SceneEntity entity) const noexcept {
    return SceneComponentQueryService::HasBehaviour(scene_, entity);
}

const BehaviourComponent* SceneBehaviourComponentQueries::TryGet(SceneEntity entity) const noexcept {
    return SceneComponentQueryService::TryGetBehaviour(scene_, entity);
}

std::size_t SceneBehaviourComponentQueries::Count(SceneEntity entity) const noexcept {
    return SceneComponentQueryService::BehaviourCount(scene_, entity);
}

const BehaviourComponent* SceneBehaviourComponentQueries::TryGetAt(SceneEntity entity, std::size_t index) const noexcept {
    return SceneComponentQueryService::TryGetBehaviourAt(scene_, entity, index);
}

const BehaviourComponent* SceneBehaviourComponentQueries::Find(SceneEntity entity, std::uint64_t assetId, BehaviourBackend backend) const noexcept {
    return SceneComponentQueryService::FindBehaviour(scene_, entity, assetId, backend);
}

std::span<const BehaviourComponent> SceneBehaviourComponentQueries::Additional(SceneEntity entity) const noexcept {
    return SceneComponentQueryService::AdditionalBehaviours(scene_, entity);
}

void SceneBehaviourComponentQueries::ForEach(BehaviourVisitor visitor, void* context) const {
    SceneIterationService::ForEachBehaviour(scene_, visitor, context);
}

const std::uint64_t* SceneBehaviourComponentQueries::RevisionCounter() const noexcept {
    return SceneComponentQueryService::BehaviourRevisionCounter(scene_);
}

std::uint64_t SceneBehaviourComponentQueries::ChangeStamp() const noexcept {
    return SceneComponentQueryService::BehaviourChangeStamp(scene_);
}

SceneBehaviourComponents::SceneBehaviourComponents(Scene& scene) noexcept
    : scene_(scene) {}

bool SceneBehaviourComponents::Has(SceneEntity entity) const noexcept {
    return SceneComponentQueryService::HasBehaviour(scene_, entity);
}

const BehaviourComponent* SceneBehaviourComponents::TryGet(SceneEntity entity) const noexcept {
    return SceneComponentQueryService::TryGetBehaviour(scene_, entity);
}

BehaviourComponent* SceneBehaviourComponents::TryGet(SceneEntity entity) noexcept {
    return SceneComponentMutationService::TryGetBehaviour(scene_, entity);
}

void SceneBehaviourComponents::Set(SceneEntity entity, const BehaviourComponent& behaviour) {
    SceneComponentMutationService::SetBehaviour(scene_, entity, behaviour);
}

void SceneBehaviourComponents::Remove(SceneEntity entity) noexcept {
    SceneComponentMutationService::RemoveBehaviour(scene_, entity);
}

void SceneBehaviourComponents::MarkModified(SceneEntity entity) noexcept {
    SceneComponentMutationService::MarkBehaviourModified(scene_, entity);
}

std::size_t SceneBehaviourComponents::Count(SceneEntity entity) const noexcept {
    return SceneComponentQueryService::BehaviourCount(scene_, entity);
}

const BehaviourComponent* SceneBehaviourComponents::TryGetAt(SceneEntity entity, std::size_t index) const noexcept {
    return SceneComponentQueryService::TryGetBehaviourAt(scene_, entity, index);
}

const BehaviourComponent* SceneBehaviourComponents::Find(SceneEntity entity, std::uint64_t assetId, BehaviourBackend backend) const noexcept {
    return SceneComponentQueryService::FindBehaviour(scene_, entity, assetId, backend);
}

std::span<const BehaviourComponent> SceneBehaviourComponents::Additional(SceneEntity entity) const noexcept {
    return SceneComponentQueryService::AdditionalBehaviours(scene_, entity);
}

bool SceneBehaviourComponents::Add(SceneEntity entity, const BehaviourComponent& behaviour) {
    return SceneComponentMutationService::AddBehaviour(scene_, entity, behaviour);
}

bool SceneBehaviourComponents::RemoveAt(SceneEntity entity, std::size_t index) noexcept {
    return SceneComponentMutationService::RemoveBehaviourAt(scene_, entity, index);
}

bool SceneBehaviourComponents::SetAt(SceneEntity entity, std::size_t index, const BehaviourComponent& behaviour) {
    return SceneComponentMutationService::SetBehaviourAt(scene_, entity, index, behaviour);
}

void SceneBehaviourComponents::SetAdditional(SceneEntity entity, std::span<const BehaviourComponent> additional) {
    SceneComponentMutationService::SetAdditionalBehaviours(scene_, entity, additional);
}

void SceneBehaviourComponents::ForEach(BehaviourVisitor visitor, void* context) const {
    SceneIterationService::ForEachBehaviour(scene_, visitor, context);
}

const std::uint64_t* SceneBehaviourComponents::RevisionCounter() const noexcept {
    return SceneComponentQueryService::BehaviourRevisionCounter(scene_);
}

std::uint64_t SceneBehaviourComponents::ChangeStamp() const noexcept {
    return SceneComponentQueryService::BehaviourChangeStamp(scene_);
}

} // namespace kb::scene
