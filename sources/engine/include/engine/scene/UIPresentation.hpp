#pragma once

#include "engine/math/EngineMath.hpp"
#include "engine/scene/SceneEntity.hpp"
#include "engine/scene/UIAssets.hpp"

#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

namespace kb::scene {

struct UIPresentationRect {
    float left = 0.0F;
    float top = 0.0F;
    float right = 0.0F;
    float bottom = 0.0F;

    [[nodiscard]] constexpr float Width() const noexcept {
        return right - left;
    }
    [[nodiscard]] constexpr float Height() const noexcept {
        return bottom - top;
    }
    [[nodiscard]] constexpr bool IsValid() const noexcept {
        return right > left && bottom > top;
    }
    [[nodiscard]] constexpr bool Contains(float x, float y) const noexcept {
        return x >= left && x < right && y >= top && y < bottom;
    }
};

// One flattened, immutable view of an authored element for a specific viewport.
// Strings remain valid until the scene reaches its next UI command boundary.
// The snapshot is derived runtime state and is never serialized.
struct UIPresentationItem {
    SceneEntity owner{};
    UIElementId elementId = 0U;
    UIPresentationRect rect{};
    UIPresentationRect clipRect{};
    kb::math::Vec2 pivot{0.5F, 0.5F};
    kb::math::Vec2 scale{1.0F, 1.0F};
    float rotationDegrees = 0.0F;
    float canvasScale = 1.0F;
    std::uint64_t painterOrder = 0U;
    UIControlKind controlKind = UIControlKind::Container;
    std::string_view text;
    bool toggleValue = false;
    float value = 0.0F;
    float minimum = 0.0F;
    float maximum = 1.0F;
    std::uint32_t selectedIndex = 0U;
    float scrollOffset = 0.0F;
    std::optional<UIPaint> paint;
    std::optional<UIImage> image;
    std::optional<UIText> textStyle;
    std::optional<UIEffects> effects;
};

// Input targets are emitted by the same traversal as presentation items. This
// keeps hit testing, clipping and painter order identical to what is rendered.
struct UIPresentationHitTarget {
    SceneEntity owner{};
    UIElementId elementId = 0U;
    UIPresentationRect rect{};
    UIPresentationRect clipRect{};
    kb::math::Vec2 pivot{0.5F, 0.5F};
    kb::math::Vec2 scale{1.0F, 1.0F};
    float rotationDegrees = 0.0F;
    std::uint64_t painterOrder = 0U;
    UIControlKind controlKind = UIControlKind::Container;
    std::string_view eventName;
};

struct UIPresentationSnapshot {
    std::uint32_t viewportWidth = 0U;
    std::uint32_t viewportHeight = 0U;
    std::uint64_t revision = 0U;
    std::vector<UIPresentationItem> items;
    std::vector<UIPresentationHitTarget> hitTargets;

    void Clear() noexcept {
        viewportWidth = 0U;
        viewportHeight = 0U;
        items.clear();
        hitTargets.clear();
    }
};

} // namespace kb::scene
