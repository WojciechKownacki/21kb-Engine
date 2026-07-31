#pragma once

#include "engine/math/EngineMath.hpp"

#include <cmath>
#include <cstdint>
#include <string_view>

namespace kb::scene {

// An authored, non-renderable box used only by the renderer's conservative
// visibility rejection. Its effective transform is the owning entity's world
// transform; no visibility state is persisted outside ECS.
struct SceneVisibilityBlockerComponent {
    static constexpr std::string_view StableId = "kb21.scene.visibility-blocker";
    static constexpr std::uint32_t SchemaVersion = 1U;

    kb::math::Vec3 localCenter{};
    kb::math::Vec3 size{ 1.0F, 1.0F, 1.0F };
    bool enabled = true;
};

[[nodiscard]] inline bool IsSceneVisibilityBlockerComponentValid(const SceneVisibilityBlockerComponent& value) noexcept {
    return std::isfinite(value.localCenter.x) && std::isfinite(value.localCenter.y) && std::isfinite(value.localCenter.z) &&
        std::isfinite(value.size.x) && std::isfinite(value.size.y) && std::isfinite(value.size.z) &&
        value.size.x > 0.0F && value.size.y > 0.0F && value.size.z > 0.0F;
}

} // namespace kb::scene
