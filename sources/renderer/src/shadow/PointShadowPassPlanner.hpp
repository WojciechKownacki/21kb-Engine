#pragma once

#include "kb/render/scene/RenderScene.hpp"
#include "kb/render/scene/SceneRenderTypes.hpp"

#include <array>
#include <cstdint>

namespace kb::render {

// Shadow-casting point lights chosen for this frame, nearest to the camera first. The result is a
// pure function of the scene and camera, so the shadow pass and the light packer agree on it.
struct PointShadowLightSelection {
    std::uint32_t count = 0U;
    std::array<const LightRenderProxyDesc*, ScenePointShadowBinding::kMaxLights> lights{};
    std::array<std::uint64_t, ScenePointShadowBinding::kMaxLights> entityIds{};
};

struct PointShadowFace {
    SceneRenderCamera camera{};
    std::uint32_t tileColumn = 0U;
    std::uint32_t tileRow = 0U;
};

struct PointShadowSetup {
    bool valid = false;
    std::uint32_t lightCount = 0U;
    std::uint32_t tileSize = 0U;
    std::uint32_t atlasWidth = 0U;
    std::uint32_t atlasHeight = 0U;
    std::array<std::array<PointShadowFace, ScenePointShadowBinding::kFaceCount>, ScenePointShadowBinding::kMaxLights> faces{};
    ScenePointShadowBinding binding{};
};

class PointShadowPassPlanner {
public:
    PointShadowPassPlanner() = delete;

    // Vertical and horizontal field of view of each cube face, a little wider than 90 degrees.
    static constexpr float kFaceFovDegrees = 95.0F;

    [[nodiscard]] static PointShadowLightSelection Select(
        const RenderScene& renderScene,
        const std::array<float, 3>* cameraPosition,
        std::uint32_t cameraCullingMask) noexcept;

    [[nodiscard]] static PointShadowSetup Build(
        const RenderScene& renderScene,
        const SceneRenderLightingConfig& lightingConfig,
        const std::array<float, 3>* cameraPosition,
        std::uint32_t cameraCullingMask,
        bgfx::TextureHandle depthTexture) noexcept;

    [[nodiscard]] static std::uint32_t TileSizeFor(const SceneRenderLightingConfig& lightingConfig) noexcept;
};

} // namespace kb::render
