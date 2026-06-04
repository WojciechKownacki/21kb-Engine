#pragma once

#include "kb/render/scene/RenderScene.hpp"
#include "kb/render/scene/SceneRenderTypes.hpp"

#include <array>
#include <cstdint>

namespace kb::render {

struct PackedSceneLighting {
    std::array<float, kMaxSceneForwardLights * 4U> dirKind{};
    std::array<float, kMaxSceneForwardLights * 4U> positionRange{};
    std::array<float, kMaxSceneForwardLights * 4U> colorIntensity{};
    std::array<float, kMaxSceneForwardLights * 4U> spot{};
    std::array<float, 4U> params{};
    std::array<float, 4U> ambient{ 0.18F, 0.20F, 0.23F, 1.0F };
    std::array<float, 4U> environmentZenith{ 0.36F, 0.42F, 0.52F, 1.0F };
    std::array<float, 4U> environmentGround{ 0.08F, 0.075F, 0.065F, 1.0F };
    std::array<float, 4U> environmentParams{ 1.0F, 1.0F, 0.25F, 0.0F };
};

class SceneLightingPacker {
public:
    SceneLightingPacker() = delete;

    [[nodiscard]] static PackedSceneLighting Build(
        const RenderScene& renderScene,
        SceneRenderSubmitStats& stats,
        SceneRenderLightingConfig config,
        const SceneRenderCamera* camera) noexcept;
    [[nodiscard]] static std::array<float, 4> CameraPosition(const SceneRenderCamera* camera) noexcept;
};

} // namespace kb::render
