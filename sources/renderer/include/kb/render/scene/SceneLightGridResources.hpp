#pragma once

#include "kb/render/scene/RenderScene.hpp"
#include <bgfx/bgfx.h>
#include <array>
#include <memory>

namespace kb::render {

struct SceneLightGridBinding {
    bgfx::TextureHandle texture = BGFX_INVALID_HANDLE;
    std::array<float, 4> minimum{}, inverseCellSize{}, dimensions{}, layout{};
    std::uint32_t lightCount = 0U;
};

// Owns derived GPU snapshots. Authored light state remains in RenderScene/ECS.
class SceneLightGridResources {
public:
    SceneLightGridResources();
    ~SceneLightGridResources();
    SceneLightGridResources(const SceneLightGridResources&) = delete;
    SceneLightGridResources& operator=(const SceneLightGridResources&) = delete;
    [[nodiscard]] bool Initialize();
    void Shutdown() noexcept;
    [[nodiscard]] SceneLightGridBinding Prepare(const RenderScene& scene,
        SceneRenderLightingConfig config, std::uint32_t cameraMask,
        std::uint64_t primaryLightId, SceneRenderSubmitStats& stats,
        SceneRenderDiagnostics* diagnostics = nullptr);
    void Bind(const SceneLightGridBinding& binding, std::uint8_t stage) const noexcept;
    void EndFrame() noexcept;
private:
    struct State;
    std::unique_ptr<State> state_;
};

} // namespace kb::render
