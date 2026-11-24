#pragma once

#include "engine/ecs/World.hpp"
#include <array>

namespace kb::network {

struct ReplicationChangeSink {
    void* context;
    void (*changed)(kb::ecs::Entity, void*);
};

template <typename T>
[[nodiscard]] std::array<kb::ecs::ObserverId, 3U> ObserveReplicationChanges(kb::ecs::World& world, ReplicationChangeSink& sink) {
    const auto changed = [](kb::ecs::Entity entity, kb::ecs::ComponentEventKind, const T*, void* context) {
        auto& destination = *static_cast<ReplicationChangeSink*>(context);
        destination.changed(entity, destination.context);
    };
    return {world.ObserveComponent<T>(kb::ecs::ComponentEventKind::Added, changed, &sink),
        world.ObserveComponent<T>(kb::ecs::ComponentEventKind::Modified, changed, &sink),
        world.ObserveComponent<T>(kb::ecs::ComponentEventKind::Removed, changed, &sink)};
}

} // namespace kb::network
