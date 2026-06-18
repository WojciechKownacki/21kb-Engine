#pragma once

#include "engine/scene/TransformComponent.hpp"

namespace kb::scene {

class TransformMath {
public:
    TransformMath() = delete;

    [[nodiscard]] static TransformComponent Identity() noexcept;
    [[nodiscard]] static bool CanUseTranslatedParentFastPath(const TransformComponent& parent) noexcept;
    [[nodiscard]] static bool CanUseUnrotatedParentFastPath(const TransformComponent& parent) noexcept;
    [[nodiscard]] static bool CanUseUnitScaleParentFastPath(const TransformComponent& parent) noexcept;
    [[nodiscard]] static bool CanUseUniformScaleParentFastPath(const TransformComponent& parent) noexcept;
    [[nodiscard]] static bool CanUseStaticLocalRotationFastPath(const TransformComponent& local) noexcept;
    [[nodiscard]] static TransformComponent ComposeRoot(const TransformComponent& local) noexcept;
    [[nodiscard]] static TransformComponent ComposeTranslatedParent(const TransformComponent& parent, const TransformComponent& local) noexcept;
    [[nodiscard]] static TransformComponent ComposeUnrotatedParent(const TransformComponent& parent, const TransformComponent& local) noexcept;
    [[nodiscard]] static TransformComponent ComposeUnitScaleParent(const TransformComponent& parent, const TransformComponent& local) noexcept;
    [[nodiscard]] static TransformComponent ComposeUniformScaleParent(const TransformComponent& parent, const TransformComponent& local) noexcept;
    [[nodiscard]] static TransformComponent ComposeStaticLocalRotationParent(const TransformComponent& parent, const TransformComponent& local) noexcept;
    [[nodiscard]] static TransformComponent Compose(const TransformComponent& parent, const TransformComponent& local) noexcept;
};

} // namespace kb::scene
