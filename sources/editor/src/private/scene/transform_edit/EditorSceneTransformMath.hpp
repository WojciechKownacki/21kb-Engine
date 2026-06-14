#pragma once

#include "engine/scene/TransformComponent.hpp"

namespace kb::editor {

class EditorSceneTransformMath {
public:
    EditorSceneTransformMath() = delete;

    [[nodiscard]] static kb::scene::Quat Normalize(kb::scene::Quat value) noexcept;
    [[nodiscard]] static kb::scene::Quat Multiply(kb::scene::Quat lhs, kb::scene::Quat rhs) noexcept;
};

} // namespace kb::editor
