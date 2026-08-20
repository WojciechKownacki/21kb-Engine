#pragma once

#include "kb/render/particles/ParticleStripGeometryBuilder.hpp"

#include <bgfx/bgfx.h>

namespace kb::render {

struct ParticleStripSubmitResult {
    bool succeeded = false;
    std::uint32_t drawCalls = 0U;
    std::uint32_t submittedIndices = 0U;
    std::uint32_t droppedSegments = 0U;
};

class ParticleStripRenderer final {
public:
    [[nodiscard]] bool Initialize();
    void Shutdown() noexcept;
    void Warmup();
    void ReleaseScene(std::uint64_t sceneId) noexcept;
    void ReleaseAllScenes() noexcept;
    [[nodiscard]] bool IsInitialized() const noexcept;
    [[nodiscard]] const ParticleStripBuildResult& Build(
        const kb::particles::ParticleRenderSnapshot& snapshot,
        const SceneRenderCamera& camera) noexcept;
    [[nodiscard]] ParticleStripSubmitResult SubmitDraw(bgfx::ViewId viewId, std::uint32_t drawIndex) noexcept;

private:
    ParticleStripGeometryBuilder geometryBuilder_;
    ParticleStripBuildResult lastBuild_{};
    bgfx::DynamicVertexBufferHandle vertexBuffer_ = BGFX_INVALID_HANDLE;
    bgfx::DynamicIndexBufferHandle indexBuffer_ = BGFX_INVALID_HANDLE;
    bgfx::ProgramHandle program_ = BGFX_INVALID_HANDLE;
};

} // namespace kb::render
