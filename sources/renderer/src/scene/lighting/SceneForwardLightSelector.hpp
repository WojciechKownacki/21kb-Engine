#pragma once

#include "kb/render/scene/RenderScene.hpp"
#include "kb/render/scene/SceneRenderTypes.hpp"

#include <array>
#include <cstdint>

namespace kb::render {

struct SceneForwardLightCandidate {
    const LightRenderProxyDesc* light = nullptr;
    std::uint64_t entityId = 0;
    float score = 0.0F;
};

struct SceneForwardLightSelection {
    std::array<SceneForwardLightCandidate, kMaxSceneForwardLights> selected{};
    std::uint32_t selectedCount = 0;
    std::uint32_t validLightCount = 0;
};

class SceneForwardLightSelector {
public:
    SceneForwardLightSelector() = delete;

    [[nodiscard]] static SceneForwardLightSelection Select(
        const RenderScene::LightProxyMap& lights,
        std::uint32_t capacity,
        const std::array<float, 4>& cameraPosition,
        SceneRenderSubmitStats& stats,
        SceneRenderLightingConfig config) noexcept;
};

} // namespace kb::render
