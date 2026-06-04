#include "engine/visual/VisualGraphBehaviourInstanceRegistry.hpp"

namespace kb::visual {

std::size_t VisualGraphBehaviourInstanceRegistry::KeyHasher::operator()(VisualGraphBehaviourInstanceKey key) const noexcept {
    const std::uint64_t mixed = key.entityId ^ (key.assetId + 0x9e3779b97f4a7c15ULL + (key.entityId << 6U) + (key.entityId >> 2U));
    return static_cast<std::size_t>(mixed);
}

VisualGraphBehaviourInstanceKey VisualGraphBehaviourInstanceRegistry::MakeKey(kb::scene::SceneEntity entity, kb::assets::AssetId assetId) noexcept {
    return VisualGraphBehaviourInstanceKey{
        .entityId = entity.Id(),
        .assetId = assetId.value,
    };
}

VisualGraphBehaviourInstance& VisualGraphBehaviourInstanceRegistry::FindOrCreate(kb::scene::SceneEntity entity, kb::assets::AssetId assetId) {
    const VisualGraphBehaviourInstanceKey key = MakeKey(entity, assetId);
    auto [iter, inserted] = instances_.try_emplace(key);
    if (inserted) {
        iter->second.entity = entity;
        iter->second.assetId = assetId;
    }
    return iter->second;
}

VisualGraphBehaviourInstance* VisualGraphBehaviourInstanceRegistry::Find(kb::scene::SceneEntity entity, kb::assets::AssetId assetId) noexcept {
    const auto iter = instances_.find(MakeKey(entity, assetId));
    return iter == instances_.end() ? nullptr : &iter->second;
}

const VisualGraphBehaviourInstance* VisualGraphBehaviourInstanceRegistry::Find(kb::scene::SceneEntity entity, kb::assets::AssetId assetId) const noexcept {
    const auto iter = instances_.find(MakeKey(entity, assetId));
    return iter == instances_.end() ? nullptr : &iter->second;
}

std::size_t VisualGraphBehaviourInstanceRegistry::Count() const noexcept {
    return instances_.size();
}

void VisualGraphBehaviourInstanceRegistry::Remove(kb::scene::SceneEntity entity, kb::assets::AssetId assetId) noexcept {
    instances_.erase(MakeKey(entity, assetId));
}

void VisualGraphBehaviourInstanceRegistry::RemoveEntity(kb::scene::SceneEntity entity) noexcept {
    for (auto iter = instances_.begin(); iter != instances_.end();) {
        if (iter->first.entityId == entity.Id()) {
            iter = instances_.erase(iter);
        } else {
            ++iter;
        }
    }
}

void VisualGraphBehaviourInstanceRegistry::RemoveAsset(kb::assets::AssetId assetId) noexcept {
    for (auto iter = instances_.begin(); iter != instances_.end();) {
        if (iter->first.assetId == assetId.value) {
            iter = instances_.erase(iter);
        } else {
            ++iter;
        }
    }
}

void VisualGraphBehaviourInstanceRegistry::Clear() noexcept {
    instances_.clear();
}

} // namespace kb::visual
