#pragma once

#include "engine/ecs/Entity.hpp"
#include "engine/ecs/QueryExecutionSettings.hpp"

#include <tuple>

namespace kb::ecs {

template <typename... ComponentTypes>
class QueryBatch {
public:
    using ComponentPointers = std::tuple<const ComponentTypes*...>;

    QueryBatch(const Entity::IdType* entityIds, std::size_t count, ComponentPointers components) noexcept
        : entityIds_(entityIds)
        , count_(count)
        , components_(components) {}

    [[nodiscard]] std::size_t Count() const noexcept { return count_; }
    [[nodiscard]] bool Empty() const noexcept { return count_ == 0; }

    [[nodiscard]] Entity EntityAt(std::size_t index) const noexcept {
        return index < count_ ? Entity{ entityIds_[index] } : Entity{};
    }

    template <std::size_t Index>
    [[nodiscard]] const auto* Components() const noexcept {
        return std::get<Index>(components_);
    }

private:
    const Entity::IdType* entityIds_ = nullptr;
    std::size_t count_ = 0;
    ComponentPointers components_{};
};

} // namespace kb::ecs
