#pragma once

namespace kb::ecs {

template <typename... Components>
Query<Components...>::Query(QueryState* state) noexcept
    : state_(state, &DestroyQueryState) {}

template <typename... Components>
bool Query<Components...>::IsValid() const noexcept {
    return IsQueryStateValid(state_.get());
}

} // namespace kb::ecs
