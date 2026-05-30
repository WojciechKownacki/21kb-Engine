#include "ecs/component/ComponentTypeCache.hpp"

namespace kb::ecs {

ComponentId ComponentTypeCache::Find(std::type_index type) const noexcept {
    const auto it = componentIds_.find(type);
    return it == componentIds_.end() ? 0 : it->second;
}

void ComponentTypeCache::Store(std::type_index type, ComponentId componentId) {
    if (componentId != 0) {
        componentIds_.emplace(type, componentId);
    }
}

void ComponentTypeCache::Clear() noexcept {
    componentIds_.clear();
}

} // namespace kb::ecs
