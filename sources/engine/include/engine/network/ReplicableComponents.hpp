#pragma once

#include "engine/network/ReplicationChangeSink.hpp"
#include "engine/ecs/ComponentReflection.hpp"

#include <cstring>
#include <type_traits>

namespace kb::network {

struct ReplicableComponent {
    std::string_view name;
    const kb::ecs::ComponentReflection* reflection = nullptr;
    bool (*read)(const kb::ecs::World&, kb::ecs::Entity, std::span<std::byte>);
    bool (*write)(kb::ecs::World&, kb::ecs::Entity, std::span<const std::byte>);
    void (*remove)(kb::ecs::World&, kb::ecs::Entity);
    kb::ecs::ComponentId (*source)(kb::ecs::World&);
    std::array<kb::ecs::ObserverId, 3U> (*observe)(kb::ecs::World&, ReplicationChangeSink&);
};

template <typename T>
[[nodiscard]] ReplicableComponent MakeReplicable(const kb::ecs::World& world, std::string_view name) {
    static_assert(std::is_trivially_copyable_v<T>);
    return {name, world.Reflection<T>(),
        [](const kb::ecs::World& source, kb::ecs::Entity entity, std::span<std::byte> bytes) {
            if (bytes.size() != sizeof(T)) return false;
            const T* existing = entity.IsValid() && source.IsAlive(entity) ? source.TryGet<T>(entity) : nullptr;
            const T value = existing == nullptr ? T{} : *existing;
            std::memcpy(bytes.data(), &value, sizeof(T));
            return existing != nullptr;
        },
        [](kb::ecs::World& destination, kb::ecs::Entity entity, std::span<const std::byte> bytes) {
            if (bytes.size() != sizeof(T) || !destination.IsAlive(entity)) return false;
            T value{};
            std::memcpy(&value, bytes.data(), sizeof(T));
            destination.Set(entity, value);
            return true;
        },
        [](kb::ecs::World& destination, kb::ecs::Entity entity) { if (destination.Has<T>(entity)) destination.Remove<T>(entity); },
        [](kb::ecs::World& destination) { return destination.RegisterComponent<T>(); },
        &ObserveReplicationChanges<T>};
}

class ReplicableComponentTable {
public:
    void Add(ReplicableComponent component) { if (component.reflection != nullptr) components_.push_back(component); }
    [[nodiscard]] const ReplicableComponent* Find(std::string_view name) const noexcept {
        for (const auto& component : components_) if (component.name == name) return &component;
        return nullptr;
    }
private:
    std::vector<ReplicableComponent> components_;
};

[[nodiscard]] ReplicableComponentTable MakeSceneReplicableComponents(const kb::ecs::World& world);

} // namespace kb::network
