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
    SceneObject object{};
    ScenePrefabOverrideFlag flags = ScenePrefabOverrideFlag::None;
};

struct ScenePrefabPropertyOverride {
    std::uint32_t nodeIndex = 0;
    SceneObject target{};
    std::string propertyPath;
    std::string value;
    SceneObject objectReference{};
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
