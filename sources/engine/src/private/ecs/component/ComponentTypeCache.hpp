#pragma once

#include "engine/ecs/ComponentId.hpp"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <typeindex>
#include <typeinfo>
#include <unordered_map>

namespace kb::ecs {

class ComponentTypeCache {
public:
    [[nodiscard]] ComponentId Find(std::type_index type) const noexcept;
    // Hot-path lookup keyed by the type_info address. Hashing a type_index hashes the decorated type
    // name on every call, which dominated component access; this table is lock-free, filled lazily
    // from the authoritative map, and never caches a miss. type_info objects duplicated across
    // modules simply occupy their own slot and resolve to the same id.
    [[nodiscard]] ComponentId Find(const std::type_info& type) const noexcept;
    void Store(std::type_index type, ComponentId componentId);
    void Clear() noexcept;
    // A module unload can free type_info objects whose addresses a later load may reuse for other
    // types, so every pointer-keyed entry made before the unload becomes untrusted.
    static void InvalidateTypeInfoEntries() noexcept;

private:
    struct FastSlot {
        std::atomic<const std::type_info*> key{ nullptr };
        std::atomic<ComponentId> id{ 0 };
        std::atomic<std::uint32_t> epoch{ 0 };
    };
    static constexpr std::size_t kFastSlots = 512U;
    static constexpr std::size_t kFastProbes = 8U;

    std::unordered_map<std::type_index, ComponentId> componentIds_;
    mutable std::array<FastSlot, kFastSlots> fast_{};
};

} // namespace kb::ecs
