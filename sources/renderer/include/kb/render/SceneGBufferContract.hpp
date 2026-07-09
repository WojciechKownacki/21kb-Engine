#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace kb::render {

enum class SceneGBufferColorAttachment : std::uint8_t {
    Albedo = 0U,
    Normal = 1U,
    Material = 2U,
    Surface = 3U,
    Count = 4U,
};

enum class SceneGBufferShadingModelId : std::uint8_t {
    Unlit = 0U,
    DefaultLit = 1U,
    Subsurface = 2U,
    ClearCoat = 3U,
    Cloth = 4U,
    Hair = 5U,
    Eye = 6U,
    SingleLayerWater = 7U,
    ThinTranslucent = 8U,
};

struct SceneGBufferClearColor {
    float red = 0.0F;
    float green = 0.0F;
    float blue = 0.0F;
    float alpha = 0.0F;

    [[nodiscard]] constexpr std::array<float, 4U> ToArray() const noexcept {
        return { red, green, blue, alpha };
    }
};

inline constexpr std::size_t kSceneGBufferColorAttachmentCount =
    static_cast<std::size_t>(SceneGBufferColorAttachment::Count);
inline constexpr float kSceneGBufferShadingModelNormalization = 1.0F / 255.0F;

[[nodiscard]] constexpr float EncodeSceneGBufferShadingModel(SceneGBufferShadingModelId model) noexcept {
    return static_cast<float>(static_cast<std::uint8_t>(model)) * kSceneGBufferShadingModelNormalization;
}

inline constexpr std::array<SceneGBufferClearColor, kSceneGBufferColorAttachmentCount> kSceneGBufferClearColors{
    SceneGBufferClearColor{ 0.0F, 0.0F, 0.0F, 0.0F },
    SceneGBufferClearColor{ 0.5F, 0.5F, 1.0F, 0.0F },
    SceneGBufferClearColor{ 0.0F, 1.0F, 1.0F, EncodeSceneGBufferShadingModel(SceneGBufferShadingModelId::DefaultLit) },
    SceneGBufferClearColor{ 0.0F, 0.0F, 0.0F, 0.5F },
};

} // namespace kb::render
