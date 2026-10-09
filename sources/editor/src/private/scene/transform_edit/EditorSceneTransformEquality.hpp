#pragma once

#include "engine/scene/TransformComponent.hpp"

namespace kb::editor {

class EditorSceneTransformEquality {
public:
    EditorSceneTransformEquality() = delete;

    [[nodiscard]] static bool Same(kb::scene::Vec3 lhs, kb::scene::Vec3 rhs) noexcept;
    [[nodiscard]] static bool Same(kb::scene::Quat lhs, kb::scene::Quat rhs) noexcept;
    [[nodiscard]] static bool Same(const kb::scene::TransformComponent& lhs, const kb::scene::TransformComponent& rhs) noexcept;
    // Same transform and the same double-precision local translation.
    [[nodiscard]] static bool Same(const kb::scene::TransformComponent& lhs, const kb::math::DVec3& lhsTranslation,
        const kb::scene::TransformComponent& rhs, const kb::math::DVec3& rhsTranslation) noexcept;
};

} // namespace kb::editor
