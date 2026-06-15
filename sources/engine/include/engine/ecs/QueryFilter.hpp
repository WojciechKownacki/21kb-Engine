#pragma once

#include "engine/ecs/ComponentId.hpp"

#include <span>
#include <vector>

namespace kb::ecs {

class QueryFilter {
public:
    QueryFilter& Require(ComponentId componentId);
    QueryFilter& Optional(ComponentId componentId);
    QueryFilter& Exclude(ComponentId componentId);
    QueryFilter& Changed(ComponentId componentId);

    [[nodiscard]] std::span<const ComponentId> RequiredComponents() const noexcept;
    [[nodiscard]] std::span<const ComponentId> OptionalComponents() const noexcept;
    [[nodiscard]] std::span<const ComponentId> ExcludedComponents() const noexcept;
    [[nodiscard]] std::span<const ComponentId> ChangedComponents() const noexcept;

private:
    std::vector<ComponentId> required_;
    std::vector<ComponentId> optional_;
    std::vector<ComponentId> excluded_;
    std::vector<ComponentId> changed_;
};

} // namespace kb::ecs
