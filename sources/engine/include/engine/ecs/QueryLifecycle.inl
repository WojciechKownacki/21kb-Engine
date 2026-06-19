#pragma once

namespace kb::ecs {

template <typename... Components>
Query<Components...>::Query(QueryState* state) noexcept
    : state_(state, &DestroyQueryState) {}

template <typename... Components>
bool Query<Components...>::IsValid() const noexcept {
    return IsQueryStateValid(state_.get());
}

template <typename... Components>
std::span<const ComponentId> Query<Components...>::ComponentIds() const noexcept {
    return QueryStateComponentIds(state_.get());
}

template <typename... Components>
std::uint64_t Query<Components...>::StructuralVersion() const noexcept {
    return QueryStateStructuralVersion(state_.get());
}

} // namespace kb::ecs
