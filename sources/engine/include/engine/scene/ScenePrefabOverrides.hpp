#pragma once

#include "engine/scene/SceneObject.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace kb::scene {

enum class ScenePrefabOverrideFlag : std::uint32_t {
    None = 0,
    Name = 1U << 0U,
    Parent = 1U << 1U,
    Transform = 1U << 2U,
    Visibility = 1U << 3U,
    Camera = 1U << 4U,
    MeshRenderer = 1U << 5U,
    Light = 1U << 6U,
    MissingObject = 1U << 7U,
    AddedChild = 1U << 8U,
    Input = 1U << 9U,
    Rigidbody = 1U << 10U,
    Collider = 1U << 11U,
    Tags = 1U << 12U,
    Behaviour = 1U << 13U,
    AudioSource = 1U << 14U,
    AudioListener = 1U << 15U,
};

[[nodiscard]] constexpr ScenePrefabOverrideFlag operator|(ScenePrefabOverrideFlag lhs, ScenePrefabOverrideFlag rhs) noexcept {
    return static_cast<ScenePrefabOverrideFlag>(static_cast<std::uint32_t>(lhs) | static_cast<std::uint32_t>(rhs));
}

[[nodiscard]] constexpr ScenePrefabOverrideFlag operator&(ScenePrefabOverrideFlag lhs, ScenePrefabOverrideFlag rhs) noexcept {
    return static_cast<ScenePrefabOverrideFlag>(static_cast<std::uint32_t>(lhs) & static_cast<std::uint32_t>(rhs));
}

constexpr ScenePrefabOverrideFlag& operator|=(ScenePrefabOverrideFlag& lhs, ScenePrefabOverrideFlag rhs) noexcept {
    lhs = lhs | rhs;
    return lhs;
}

[[nodiscard]] constexpr bool HasPrefabOverride(ScenePrefabOverrideFlag flags, ScenePrefabOverrideFlag flag) noexcept {
    return (flags & flag) != ScenePrefabOverrideFlag::None;
}

struct ScenePrefabNodeOverride {
    std::uint32_t nodeIndex = 0;
    std::uint64_t nodeId = 0;
    SceneObject object{};
    ScenePrefabOverrideFlag flags = ScenePrefabOverrideFlag::None;
};

struct ScenePrefabPropertyOverride {
    std::uint32_t nodeIndex = 0;
    std::uint64_t nodeId = 0;
    SceneObject target{};
    std::string propertyPath;
    std::string value;
    SceneObject objectReference{};
    // LIB-092: the stable, within-instance node id of `objectReference`'s
    // target (0 / ScenePrefabNodeDesc::InvalidStableId if the reference
    // does not correspond to another node of THIS instance — e.g. a
    // "parent" override where the new parent lies entirely outside the
    // prefab's own structure). `objectReference` itself is a live
    // SceneObject that does not survive a save/load round trip; this id
    // does, by mirroring the same nodeId-based resolution `target`/
    // nodeIndex already use (ScenePrefab::ResolveNodeIndex /
    // FindNodeIndexByStableId) — populated when the override is first
    // detected (ScenePrefabObjectOverrideReporter), written to and read
    // back from the prefab asset file (ScenePrefabAssetVariantWriter /
    // ScenePrefabAssetOverrideReader), and re-resolved to a live
    // SceneObject at instantiation time (ScenePrefabInstanceSynchronizer).
    std::uint64_t objectReferenceNodeId = 0;
    ScenePrefabOverrideFlag flag = ScenePrefabOverrideFlag::None;
};

struct ScenePrefabOverrideReport {
    std::vector<ScenePrefabNodeOverride> nodes;
    std::vector<ScenePrefabPropertyOverride> properties;

    [[nodiscard]] bool Empty() const noexcept {
        return nodes.empty() && properties.empty();
    }
};

} // namespace kb::scene
