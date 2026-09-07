#pragma once

#include "engine/scene/UIPresentation.hpp"

#include <array>
#include <cstdint>
#include <vector>

namespace kb::render {

enum class UserWidgetTextureSource : std::uint8_t {
    None,
    ImageAsset,
    FontAtlas,
    BackgroundBlur,
};

enum class UserWidgetFragmentKind : std::uint8_t {
    Shape,
    Image,
    Font,
    BackgroundBlur,
};

struct UserWidgetVertex {
    float x = 0.0F;
    float y = 0.0F;
    float u = 0.0F;
    float v = 0.0F;
    float localX = 0.0F;
    float localY = 0.0F;
};

struct UserWidgetDrawStyle {
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
    UserWidgetFragmentKind fragmentKind = UserWidgetFragmentKind::Shape;

    [[nodiscard]] bool operator==(const UserWidgetDrawStyle&) const noexcept = default;
};

struct UserWidgetTextureKey {
    UserWidgetTextureSource source = UserWidgetTextureSource::None;
    std::uint64_t assetId = 0U;
    std::uint32_t pixelSize = 0U;

    [[nodiscard]] bool operator==(const UserWidgetTextureKey&) const noexcept = default;
};

struct UserWidgetDrawBatch {
    UserWidgetTextureKey texture{};
    kb::scene::UIPresentationRect clipRect{};
    UserWidgetDrawStyle style{};
    std::uint32_t firstIndex = 0U;
    std::uint32_t indexCount = 0U;
};

struct UserWidgetDrawList {
    std::vector<UserWidgetVertex> vertices;
    std::vector<std::uint32_t> indices;
    std::vector<UserWidgetDrawBatch> batches;
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

struct UserWidgetImageBinding {
    std::uint64_t assetId = 0U;
    std::uint16_t width = 0U;
    std::uint16_t height = 0U;
};

struct UserWidgetGlyphQuad {
    float left = 0.0F;
    float top = 0.0F;
    float right = 0.0F;
    float bottom = 0.0F;
    float u0 = 0.0F;
    float v0 = 0.0F;
    float u1 = 0.0F;
    float v1 = 0.0F;
};

struct UserWidgetTextRun {
    std::uint64_t ownerId = 0U;
    kb::scene::UIElementId elementId = 0U;
    std::uint64_t fontAssetId = 0U;
    std::uint32_t pixelSize = 0U;
    std::uint16_t atlasWidth = 0U;
    std::uint16_t atlasHeight = 0U;
    std::vector<UserWidgetGlyphQuad> glyphs;
};

} // namespace kb::render
