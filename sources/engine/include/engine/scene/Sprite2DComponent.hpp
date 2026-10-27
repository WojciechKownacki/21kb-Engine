#pragma once

#include <cmath>
#include <cstdint>
#include <string_view>

namespace kb::scene {

// A picture, or a cell of a sprite sheet, drawn on its own in a 2D scene, with a frame animation when FrameCount is
// above 1 (an AnimatedSprite). The World 2D plugin turns it into a quad that lives in the scene as a hidden child.
struct Sprite2DComponent {
    static constexpr std::string_view StableId = "kb21.scene.sprite2d";
    static constexpr std::uint32_t SchemaVersion = 1U;

    std::uint64_t imageAssetId = 0U;
    std::uint64_t materialAssetId = 0U;
    std::uint32_t cellWidth = 0U;
    std::uint32_t cellHeight = 0U;
    std::uint32_t margin = 0U;
    std::uint32_t spacing = 0U;
    std::uint32_t imageWidth = 0U;
    std::uint32_t imageHeight = 0U;
    std::uint32_t cell = 0U;
    std::uint32_t frameCount = 0U;
    float framesPerSecond = 8.0F;
    bool loop = true;
    bool playing = true;
    float time = 0.0F;
    float pixelsPerUnit = 100.0F;
    float pivotX = 0.5F;
    float pivotY = 0.5F;
    bool flipX = false;
    bool flipY = false;
    float tintR = 1.0F;
    float tintG = 1.0F;
    float tintB = 1.0F;
    float tintA = 1.0F;
    std::int32_t sortingOrder = 0;
    bool wall = true;
    bool visible = true;
    bool operator==(const Sprite2DComponent&) const = default;
};

[[nodiscard]] inline bool IsSprite2DComponentValid(const Sprite2DComponent& value) noexcept {
    const auto finite = [](float number, float low, float high) noexcept { return std::isfinite(number) && number >= low && number <= high; };
    return finite(value.framesPerSecond, 0.0F, 1000.0F) && finite(value.time, 0.0F, 1.0e7F) &&
        finite(value.pixelsPerUnit, 0.0001F, 1.0e6F) && finite(value.pivotX, -16.0F, 16.0F) && finite(value.pivotY, -16.0F, 16.0F) &&
        finite(value.tintR, 0.0F, 1.0F) && finite(value.tintG, 0.0F, 1.0F) && finite(value.tintB, 0.0F, 1.0F) && finite(value.tintA, 0.0F, 1.0F) &&
        value.sortingOrder >= -1000 && value.sortingOrder <= 1000 && value.cellWidth <= 16384U && value.cellHeight <= 16384U &&
        value.margin <= 4096U && value.spacing <= 4096U && value.imageWidth <= 65536U && value.imageHeight <= 65536U &&
        value.frameCount <= 4096U && value.cell <= 16777216U;
}

} // namespace kb::scene
