#pragma once

#include "kb/render/scene/RenderScene.hpp"
#include "kb/render/scene/SceneRenderTypes.hpp"

#include <array>
#include <cstdint>

namespace kb::render {

// Shadow-casting point and spot lights chosen for this frame, the most important first (see PointShadowPassPlanner::Select).
// The result depends only on the scene, the camera, the lighting config and the lights that held a row, so the shadow
// pass and the light packer agree on it.
struct PointShadowLightSelection {
    std::uint32_t count = 0U;
    // Shadow-casting lights that did not fit in kMaxLights and so are lit without a shadow this frame.
    std::uint32_t droppedCount = 0U;
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
    // The atlas rows in use: the highest row of a selected light + 1. A row between two lights that left is
    // empty (entity 0, no faces); the shader skips it.
    std::uint32_t lightCount = 0U;
    // The lights that got a row this frame.
    std::uint32_t selectedLights = 0U;
    // Eligible shadow-casting lights beyond the limit (see PointShadowLightSelection::droppedCount).
    std::uint32_t droppedLights = 0U;
    std::uint32_t tileSize = 0U;
    // Faces rendered per light: all of them for a point light, only the first for a spot light.
    std::array<std::uint32_t, ScenePointShadowBinding::kMaxLights> faceCount{};
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
    // A spot frustum is this much wider than the cone so that filtering at the cone edge stays inside the tile.
    static constexpr float kSpotMarginDegrees = 6.0F;

    // A light's importance is its peak radiance times the square of the share of the view its reach covers from the
    // camera, so a bright light that fills the view outranks a dim or far one; a light whose reach is under 3% of its
    // distance is not worth a shadow pass. A light that held a row (`incumbents`, ShadowMapResource::RowOwners) counts
    // 25% more, so two lights of about the same importance do not trade rows every frame. Ties go to the nearer light, then
    // the lower entity id. `config` limits the count and turns the cube shadows of point and area lights off.
    [[nodiscard]] static PointShadowLightSelection Select(
        const RenderScene& renderScene,
        const std::array<float, 3>* cameraPosition,
        std::uint32_t cameraCullingMask,
        const SceneRenderLightingConfig& config = {},
        const std::array<std::uint64_t, ScenePointShadowBinding::kMaxLights>* incumbents = nullptr) noexcept;

    // `rowOwners` is the row of each light from the previous frame (ShadowMapResource::RowOwners) and is updated: a
    // light that stays selected keeps its row, a new one takes the lowest free row. When the rows have grown
    // sparse enough to need larger atlas tiles than the lights alone would, they are dealt out again.
    [[nodiscard]] static PointShadowSetup Build(
        const RenderScene& renderScene,
        const SceneRenderLightingConfig& lightingConfig,
        const std::array<float, 3>* cameraPosition,
        std::uint32_t cameraCullingMask,
        bgfx::TextureHandle depthTexture,
        std::array<std::uint64_t, ScenePointShadowBinding::kMaxLights>& rowOwners) noexcept;

    [[nodiscard]] static std::uint32_t TileSizeFor(const SceneRenderLightingConfig& lightingConfig, std::uint32_t lightCount) noexcept;
};

} // namespace kb::render
