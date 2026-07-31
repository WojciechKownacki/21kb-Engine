#pragma once

#include <cstdint>
#include <string_view>

namespace kb::scene {

// The authored source is an asset id. Runtime-created entity handles and
// loaded-scene ids deliberately do not live here: they are derived resources
// owned by ContentInstanceSceneSystem and may be rebuilt at any time.
enum class ContentInstanceKind : std::uint8_t {
    Prefab = 0U,
    Subscene = 1U,
    WorldFragment = 2U,
};

// Owner content is attached to and released with the component owner.
// Persistent content is detached from an owner that dies and remains in the
// world until its own lifetime is managed by the loaded-content/prefab APIs.
enum class ContentInstanceLifetime : std::uint8_t {
    Owner = 0U,
    Persistent = 1U,
};

struct ContentInstanceComponent {
    static constexpr std::string_view StableId = "kb21.scene.content-instance";
    static constexpr std::uint32_t SchemaVersion = 1U;

    std::uint64_t assetId = 0U;
    ContentInstanceKind kind = ContentInstanceKind::Prefab;
    ContentInstanceLifetime lifetime = ContentInstanceLifetime::Owner;
    bool active = true;
};

[[nodiscard]] constexpr bool IsContentInstanceKindValid(ContentInstanceKind kind) noexcept {
    return kind == ContentInstanceKind::Prefab || kind == ContentInstanceKind::Subscene || kind == ContentInstanceKind::WorldFragment;
}

[[nodiscard]] constexpr bool IsContentInstanceLifetimeValid(ContentInstanceLifetime lifetime) noexcept {
    return lifetime == ContentInstanceLifetime::Owner || lifetime == ContentInstanceLifetime::Persistent;
}

} // namespace kb::scene
