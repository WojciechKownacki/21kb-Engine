#pragma once

#include "engine/scene/SceneEntity.hpp"

#include <cstddef>

namespace kb::scene {

class SceneState;

[[nodiscard]] std::size_t SceneChildDepth(const SceneState& state, SceneEntity parent) noexcept;
[[nodiscard]] bool FitsSceneHierarchyDepth(const SceneState& state, SceneEntity child, SceneEntity parent) noexcept;

} // namespace kb::scene
