#pragma once

#include "engine/math/EngineMath.hpp"
#include "engine/ui/layout/UIEdges.hpp"

#include <cstdint>
#include <string_view>

namespace kb::scene {

enum class UIImageScaleMode : std::uint8_t {
    Stretch,
    Contain,
    Cover,
    // Repeats the image at its own size.
    Tiled,
};

enum class UIImageFillMethod : std::uint8_t {
    // The whole image shows.
    None,
    // Only `fillAmount` of the image shows, cut across its width, its height or around its centre -
    // health bars, loading bars and cooldown dials.
    Horizontal,
    Vertical,
    Radial360,
};

enum class UIImageFillOrigin : std::uint8_t {
    // Left, bottom, or twelve o'clock for a radial fill.
    Start,
    // Right, top, or six o'clock for a radial fill.
    End,
};

struct UIImage {
    static constexpr std::string_view StableId = "kb21.ui.image";
    static constexpr std::uint32_t SchemaVersion = 2U;

    std::uint64_t imageAssetId = 0U;
    kb::math::Rect uvRect{0.0F, 0.0F, 1.0F, 1.0F};
    UIImageScaleMode scaleMode = UIImageScaleMode::Stretch;
    bool preserveAspect = false;
    UIEdges nineSlice{};
    kb::math::Color color{1.0F, 1.0F, 1.0F, 1.0F};
    UIImageFillMethod fillMethod = UIImageFillMethod::None;
    UIImageFillOrigin fillOrigin = UIImageFillOrigin::Start;
    float fillAmount = 1.0F;
    bool fillClockwise = true;
    // A press only lands where the image is at least this opaque; 0 makes the whole rectangle clickable.
    float alphaHitThreshold = 0.0F;
};

} // namespace kb::scene
