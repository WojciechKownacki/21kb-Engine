#pragma once

#include <array>
#include <cstdint>
#include <vector>

namespace kb::render {

enum class ScreenUITextureSource : std::uint8_t {
    None,
    ImageAssetSrgb,
    ImageAssetLinear,
    FontAtlas,
    BackgroundBlur,
};

enum class ScreenUIFragmentKind : std::uint8_t {
    Shape,
    Image,
    Font,
    BackgroundBlur,
};

struct ScreenUIRect {
    float left = 0.0F;
    float top = 0.0F;
    float right = 0.0F;
    float bottom = 0.0F;

    [[nodiscard]] constexpr bool IsValid() const noexcept {
        return right > left && bottom > top;
    }

    [[nodiscard]] constexpr bool operator==(const ScreenUIRect&) const noexcept = default;
};

struct ScreenUIVertex {
    float x = 0.0F;
    float y = 0.0F;
    float u = 0.0F;
    float v = 0.0F;
    float localX = 0.0F;
    float localY = 0.0F;
};

struct ScreenUIDrawStyle {
    std::array<float, 4> fillColor{};
    std::array<float, 4> borderColor{};
    std::array<float, 4> borderWidths{};
    std::array<float, 4> cornerRadii{};
    float width = 0.0F;
    float height = 0.0F;
    float opacity = 1.0F;
    float feather = 0.0F;
    float fontOutlineWidth = 0.0F;
    float blurStrength = 0.0F;
    ScreenUIFragmentKind fragmentKind = ScreenUIFragmentKind::Shape;

    [[nodiscard]] bool operator==(const ScreenUIDrawStyle&) const noexcept = default;
};

struct ScreenUITextureKey {
    ScreenUITextureSource source = ScreenUITextureSource::None;
    std::uint64_t assetId = 0U;
    std::uint32_t pixelSize = 0U;

    [[nodiscard]] bool operator==(const ScreenUITextureKey&) const noexcept = default;
};

struct ScreenUIDrawBatch {
    ScreenUITextureKey texture{};
    ScreenUIRect clipRect{};
    ScreenUIDrawStyle style{};
    std::uint32_t firstIndex = 0U;
    std::uint32_t indexCount = 0U;
};

struct ScreenUIDrawList {
    std::vector<ScreenUIVertex> vertices;
    std::vector<std::uint32_t> indices;
    std::vector<ScreenUIDrawBatch> batches;
    bool requiresBackgroundBlur = false;
    float maximumBackgroundBlur = 0.0F;

    void Clear() noexcept {
        vertices.clear();
        indices.clear();
        batches.clear();
        requiresBackgroundBlur = false;
        maximumBackgroundBlur = 0.0F;
    }
};

struct ScreenUIImageBinding {
    std::uint64_t assetId = 0U;
    ScreenUITextureSource source = ScreenUITextureSource::ImageAssetSrgb;
    std::uint16_t width = 0U;
    std::uint16_t height = 0U;
};

struct ScreenUIGlyphQuad {
    float left = 0.0F;
    float top = 0.0F;
    float right = 0.0F;
    float bottom = 0.0F;
    float u0 = 0.0F;
    float v0 = 0.0F;
    float u1 = 0.0F;
    float v1 = 0.0F;
    std::array<float, 4U> color{1.0F, 1.0F, 1.0F, 1.0F};
    // Byte offset of this glyph in the authored string, carried from the markup parser so a
    // text caret can be placed at its left edge.
    std::uint32_t sourceOffset = 0U;
    // Baseline extent of the line this glyph sits on, so a caret spans the line rather than
    // the glyph's own ink box.
    float lineTop = 0.0F;
    float lineBottom = 0.0F;
    // Pen position after this glyph. A caret sitting past the last character belongs here, not
    // at the glyph's ink edge, which excludes the advance's right side bearing.
    float advanceRight = 0.0F;
};

struct ScreenUITextRun {
    std::uint64_t entity = 0U;
    std::uint64_t fontAssetId = 0U;
    std::uint32_t pixelSize = 0U;
    std::uint16_t atlasWidth = 0U;
    std::uint16_t atlasHeight = 0U;
    std::vector<ScreenUIGlyphQuad> glyphs;
};

} // namespace kb::render
