#include "ecs/component/ComponentTypeCatalog.hpp"

#include <utility>

namespace kb::ecs {

void ComponentTypeCatalog::Add(ComponentTypeInfo typeInfo) {
    if (typeInfo.id != 0 && typeInfo.size != 0 && typeInfo.alignment != 0) {
        componentTypes_.push_back(std::move(typeInfo));
    }
}

const ComponentTypeInfo* ComponentTypeCatalog::Find(ComponentId componentId) const noexcept {
    for (const ComponentTypeInfo& typeInfo : componentTypes_) {
        if (typeInfo.id == componentId) {
            return &typeInfo;
        }
    }
    return nullptr;
}

std::span<const ComponentTypeInfo> ComponentTypeCatalog::Types() const noexcept {
    return componentTypes_;
}

void ComponentTypeCatalog::Clear() noexcept {
    componentTypes_.clear();
}

} // namespace kb::ecs
