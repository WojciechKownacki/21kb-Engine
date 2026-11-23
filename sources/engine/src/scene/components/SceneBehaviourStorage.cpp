#include "scene/components/SceneBehaviourComponentStore.hpp"

#include "scene/components/SceneComponentAccess.hpp"
#include "scene/components/SceneComponentStorageAccess.hpp"

#include <algorithm>
#include <utility>

namespace kb::scene {

SceneBehaviourComponentStore::SceneBehaviourComponentStore(kb::ecs::World& world, std::uint64_t componentId) noexcept
    : world_(&world)
    , componentId_(componentId) {}

bool SceneBehaviourComponentStore::Has(SceneEntity entity) const noexcept {
    return SceneComponentStorageAccess::Has<BehaviourComponent>(world_, entity);
}

const BehaviourComponent* SceneBehaviourComponentStore::TryGet(SceneEntity entity) const noexcept {
    return SceneComponentStorageAccess::TryGet<BehaviourComponent>(world_, entity);
}

BehaviourComponent* SceneBehaviourComponentStore::TryGet(SceneEntity entity) noexcept {
    return SceneComponentStorageAccess::TryGetMutable<BehaviourComponent>(world_, entity);
}

void SceneBehaviourComponentStore::Set(SceneEntity entity, const BehaviourComponent& behaviour) {
    ++revision_;
    if (const auto found = additional_.find(entity.Id()); found != additional_.end()) {
        std::erase_if(found->second, [&behaviour](const BehaviourComponent& other) {
            return other.behaviourAssetId == behaviour.behaviourAssetId && other.backend == behaviour.backend;
        });
        if (found->second.empty()) {
            additional_.erase(found);
        }
    }
    SceneComponentStorageAccess::Set<BehaviourComponent>(world_, entity, behaviour);
}

void SceneBehaviourComponentStore::Remove(SceneEntity entity) noexcept {
    ++revision_;
    additional_.erase(entity.Id());
    SceneComponentStorageAccess::Remove<BehaviourComponent>(world_, entity);
}

std::size_t SceneBehaviourComponentStore::Count(SceneEntity entity) const noexcept {
    if (!Has(entity)) {
        return 0U;
    }
    return 1U + Additional(entity).size();
}

const BehaviourComponent* SceneBehaviourComponentStore::TryGetAt(SceneEntity entity, std::size_t index) const noexcept {
    if (index == 0U) {
        return TryGet(entity);
    }
    const std::span<const BehaviourComponent> others = Additional(entity);
    return index - 1U < others.size() && Has(entity) ? &others[index - 1U] : nullptr;
}

std::size_t SceneBehaviourComponentStore::IndexOf(SceneEntity entity, std::uint64_t assetId, BehaviourBackend backend) const noexcept {
    const BehaviourComponent* first = TryGet(entity);
    if (first == nullptr) {
        return kNoBehaviour;
    }
    if (first->behaviourAssetId == assetId && first->backend == backend) {
        return 0U;
    }
    const std::span<const BehaviourComponent> others = Additional(entity);
    for (std::size_t index = 0U; index < others.size(); ++index) {
        if (others[index].behaviourAssetId == assetId && others[index].backend == backend) {
            return index + 1U;
        }
    }
    return kNoBehaviour;
}

std::span<const BehaviourComponent> SceneBehaviourComponentStore::Additional(SceneEntity entity) const noexcept {
    if (additional_.empty()) {
        return {};
    }
    const auto found = additional_.find(entity.Id());
    return found == additional_.end() ? std::span<const BehaviourComponent>{} : std::span<const BehaviourComponent>{ found->second };
}

bool SceneBehaviourComponentStore::Add(SceneEntity entity, const BehaviourComponent& behaviour) {
    if (!Has(entity)) {
        Set(entity, behaviour);
        return true;
    }
    if (IndexOf(entity, behaviour.behaviourAssetId, behaviour.backend) != kNoBehaviour || Count(entity) >= kMaxBehavioursPerEntity) {
        return false;
    }
    additional_[entity.Id()].push_back(behaviour);
    ++revision_;
    return true;
}

bool SceneBehaviourComponentStore::RemoveAt(SceneEntity entity, std::size_t index) noexcept {
    if (index >= Count(entity)) {
        return false;
    }
    const auto found = additional_.find(entity.Id());
    if (index == 0U) {
        if (found == additional_.end()) {
            Remove(entity);
            return true;
        }
        // The next behaviour becomes the component. Set would drop an additional entry of the same script, so the list is
        // taken first and put back after.
        std::vector<BehaviourComponent> rest = std::move(found->second);
        additional_.erase(found);
        const BehaviourComponent next = rest.front();
        rest.erase(rest.begin());
        Set(entity, next);
        if (!rest.empty()) {
            additional_[entity.Id()] = std::move(rest);
        }
        return true;
    }
    found->second.erase(found->second.begin() + static_cast<std::ptrdiff_t>(index - 1U));
    if (found->second.empty()) {
        additional_.erase(found);
    }
    ++revision_;
    return true;
}

bool SceneBehaviourComponentStore::SetAt(SceneEntity entity, std::size_t index, const BehaviourComponent& behaviour) {
    if (index >= Count(entity)) {
        return false;
    }
    if (index == 0U) {
        Set(entity, behaviour);
        return true;
    }
    additional_[entity.Id()][index - 1U] = behaviour;
    ++revision_;
    return true;
}

void SceneBehaviourComponentStore::SetAdditional(SceneEntity entity, std::span<const BehaviourComponent> additional) {
    if (additional.empty() || !Has(entity)) {
        if (additional_.erase(entity.Id()) != 0U) {
            ++revision_;
        }
        return;
    }
    std::vector<BehaviourComponent> list;
    const BehaviourComponent first = *TryGet(entity);
    for (const BehaviourComponent& behaviour : additional) {
        const bool repeated = (behaviour.behaviourAssetId == first.behaviourAssetId && behaviour.backend == first.backend) ||
            std::ranges::any_of(list, [&behaviour](const BehaviourComponent& other) {
                return other.behaviourAssetId == behaviour.behaviourAssetId && other.backend == behaviour.backend;
            });
        if (!repeated && list.size() + 1U < kMaxBehavioursPerEntity) {
            list.push_back(behaviour);
        }
    }
    if (list.empty()) {
        additional_.erase(entity.Id());
    } else {
        additional_[entity.Id()] = std::move(list);
    }
    ++revision_;
}

void SceneBehaviourComponentStore::ForEachAdditional(void (*visitor)(SceneEntity, const BehaviourComponent&, void*), void* context) const {
    if (additional_.empty() || visitor == nullptr) {
        return;
    }
    // A visitor may change the behaviours of the scene, so it is given a copy.
    std::vector<std::pair<SceneEntity, BehaviourComponent>> snapshot;
    for (const auto& [id, list] : additional_) {
        if (!Has(SceneEntity{ id })) {
            continue;
        }
        for (const BehaviourComponent& behaviour : list) {
            snapshot.emplace_back(SceneEntity{ id }, behaviour);
        }
    }
    std::ranges::stable_sort(snapshot, [](const auto& lhs, const auto& rhs) { return lhs.first.Id() < rhs.first.Id(); });
    for (const auto& [entity, behaviour] : snapshot) {
        visitor(entity, behaviour, context);
    }
}

void SceneBehaviourComponentStore::MarkModified(SceneEntity entity) noexcept {
    ++revision_;
    SceneComponentStorageAccess::MarkModified<BehaviourComponent>(world_, entity);
}

void SceneBehaviourComponentStore::NoteEntityDestroyed(SceneEntity entity) noexcept {
    if (Has(entity)) {
        ++revision_;
    }
    additional_.erase(entity.Id());
}

std::uint64_t SceneBehaviourComponentStore::ChangeStamp() const noexcept {
    return revision_ + world_->NativeStorage().ComponentChangeStamp(componentId_);
}

} // namespace kb::scene
