#pragma once

#include "engine/scene/TransformComponent.hpp"

namespace kb::scene {

class TransformMath {
public:
    TransformMath() = delete;

    [[nodiscard]] static TransformComponent Identity() noexcept;
    [[nodiscard]] static TransformComponent Compose(const TransformComponent& parent, const TransformComponent& local) noexcept;
};

} // namespace kb::scene
