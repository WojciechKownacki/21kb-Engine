#pragma once

#include "kb/render/particles/ParticleRenderBatcher.hpp"
#include "engine/particles/ParticleRenderCapabilities.hpp"

#include <bgfx/bgfx.h>

#include <array>
#include <cstdint>
#include <span>

namespace kb::render {

// Renderer-owned visual-state compute pass. The authoritative fixed-step source
// is uploaded once per scene revision; the resulting instance buffer is then
// consumed by draw submission without a CPU readback or a fence wait.
class ParticleGpuVisualSimulation final {
public:
    [[nodiscard]] bool Initialize();
    void Shutdown() noexcept;
    [[nodiscard]] bool Prepare(
        bgfx::ViewId viewId,
        std::uint64_t sceneId,
        std::uint64_t fixedStepIndex,
        std::span<const ParticleGpuInstance> source,
        std::span<const std::uint32_t> visualMask) noexcept;
    [[nodiscard]] bool HasPreparedView(
        bgfx::ViewId viewId,
        std::uint64_t sceneId,
        std::uint64_t fixedStepIndex) const noexcept;
    [[nodiscard]] bgfx::DynamicVertexBufferHandle InstanceBuffer() const noexcept;
    [[nodiscard]] kb::particles::ParticleGpuVisualAvailability Availability() const noexcept;
    void ReleaseScene(std::uint64_t sceneId) noexcept;
    void ReleaseAllScenes() noexcept;

private:
    [[nodiscard]] bool EnsureCapacity(std::uint32_t requestedCapacity) noexcept;
    void DestroyBuffers() noexcept;

    bgfx::ProgramHandle program_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle paramsUniform_ = BGFX_INVALID_HANDLE;
    bgfx::DynamicVertexBufferHandle sourceBuffer_ = BGFX_INVALID_HANDLE;
    bgfx::DynamicVertexBufferHandle outputBuffer_ = BGFX_INVALID_HANDLE;
    std::array<bgfx::DynamicVertexBufferHandle, 2U> positionBuffers_{{
        BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE}};
    std::array<bgfx::DynamicVertexBufferHandle, 2U> velocityBuffers_{{
        BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE}};
    bgfx::DynamicIndexBufferHandle visualMask_ = BGFX_INVALID_HANDLE;
    bgfx::DynamicIndexBufferHandle aliveCounters_ = BGFX_INVALID_HANDLE;
    bgfx::IndirectBufferHandle indirectArgs_ = BGFX_INVALID_HANDLE;
    std::uint32_t capacity_ = 0U;
    std::uint64_t preparedSceneId_ = 0U;
    std::uint64_t preparedFixedStepIndex_ = 0U;
    bgfx::ViewId preparedViewId_ = UINT16_MAX;
    std::uint8_t stateReadIndex_ = 0U;
};

} // namespace kb::render
