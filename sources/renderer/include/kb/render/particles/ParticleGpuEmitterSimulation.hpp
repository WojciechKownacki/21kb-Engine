#pragma once

#include "engine/particles/ParticleGpuEmitter.hpp"

#include <bgfx/bgfx.h>

#include <array>
#include <cstdint>
#include <span>
#include <unordered_map>
#include <vector>

namespace kb::render {

// Renderer side of GPU-simulated particle emitters. Owns, per emitter, a ring buffer of birth records
// and the instance buffer a compute pass rebuilds every frame from them; nothing is read back.
class ParticleGpuEmitterSimulation final {
public:
    struct Draw {
        const kb::particles::ParticleGpuEmitterParams* params = nullptr;
        bgfx::DynamicVertexBufferHandle instances = BGFX_INVALID_HANDLE;
        std::uint32_t capacity = 0U;
    };

    [[nodiscard]] bool Initialize();
    void Shutdown() noexcept;
    [[nodiscard]] bool IsReady() const noexcept;

    // Applies one frame of drained commands for a scene: creates emitters, uploads spawn records,
    // releases emitters.
    void Apply(std::uint64_t sceneId, std::span<const kb::particles::ParticleGpuEmitterCommand> commands) noexcept;
    // Moves the scene render clock forward by the frame time, never more than one fixed step away
    // from the newest simulated time.
    void Advance(std::uint64_t sceneId, float frameDeltaSeconds) noexcept;
    // What the frame gives the simulation: the scene depth the camera has drawn so far (colliding emitters
    // bounce off it) and the camera position (alpha-blended emitters are sorted back to front from it).
    struct FrameContext {
        bgfx::TextureHandle depthTexture = BGFX_INVALID_HANDLE;
        std::array<float, 16> viewProjection{};
        std::array<float, 16> inverseViewProjection{};
        std::array<float, 4> cameraPosition{};
        std::array<float, 2> texelSize{};
        bool homogeneousDepth = false;
    };

    // Rebuilds the instance buffers of the scene emitters; call once per rendered frame.
    void Dispatch(bgfx::ViewId viewId, std::uint64_t sceneId, const FrameContext& frame) noexcept;
    // Emitters of the scene that were dispatched this frame.
    [[nodiscard]] std::span<const Draw> Draws(std::uint64_t sceneId) noexcept;
    [[nodiscard]] bool HasEmitters(std::uint64_t sceneId) const noexcept;

    void ReleaseScene(std::uint64_t sceneId) noexcept;
    void ReleaseAllScenes() noexcept;
    [[nodiscard]] std::uint64_t AllocatedBytes() const noexcept { return allocatedBytes_; }

private:
    struct Key {
        std::uint64_t sceneId = 0U;
        std::uint64_t instanceId = 0U;
        std::uint64_t emitterId = 0U;
        [[nodiscard]] bool operator==(const Key&) const noexcept = default;
    };
    struct KeyHash {
        [[nodiscard]] std::size_t operator()(const Key& key) const noexcept {
            std::uint64_t hash = key.sceneId * 0x9E3779B97F4A7C15ULL;
            hash ^= key.instanceId + 0x9E3779B97F4A7C15ULL + (hash << 6U) + (hash >> 2U);
            hash ^= key.emitterId + 0x9E3779B97F4A7C15ULL + (hash << 6U) + (hash >> 2U);
            return static_cast<std::size_t>(hash);
        }
    };
    struct Emitter {
        kb::particles::ParticleGpuEmitterParams params{};
        // Owner matrix of a local-space emitter (identity for world-space ones) and its inverse.
        std::array<float, 16> world{ 1.0F, 0.0F, 0.0F, 0.0F, 0.0F, 1.0F, 0.0F, 0.0F, 0.0F, 0.0F, 1.0F, 0.0F, 0.0F, 0.0F, 0.0F, 1.0F };
        std::array<float, 16> worldInverse = world;
        bgfx::DynamicVertexBufferHandle spawns = BGFX_INVALID_HANDLE;
        bgfx::DynamicVertexBufferHandle instances = BGFX_INVALID_HANDLE;
        // Per-slot position/velocity record of the colliding kernel; only created for colliding emitters.
        bgfx::DynamicVertexBufferHandle state = BGFX_INVALID_HANDLE;
        // Alpha-blended emitters only: sort keys (distance, slot) padded to a power of two, and the
        // instance records in back-to-front order, which is what gets drawn.
        bgfx::DynamicVertexBufferHandle sortKeys = BGFX_INVALID_HANDLE;
        bgfx::DynamicVertexBufferHandle sortedInstances = BGFX_INVALID_HANDLE;
        std::uint32_t capacity = 0U;
        std::uint64_t nextSlot = 0U;
        std::uint64_t bytes = 0U;
    };
    struct SceneClock {
        double latest = 0.0;
        double now = 0.0;
        std::vector<Draw> draws;
    };

    [[nodiscard]] bool Create(const Key& key, const kb::particles::ParticleGpuEmitterParams& params) noexcept;
    void Destroy(Emitter& emitter) noexcept;
    void Upload(Emitter& emitter, std::span<const kb::particles::ParticleGpuSpawn> spawns) noexcept;
    void Clear(Emitter& emitter) noexcept;
    // Creates the state buffer of a colliding emitter; false when the memory budget would be exceeded.
    [[nodiscard]] bool EnsureState(Emitter& emitter) noexcept;
    // Creates the sort buffers of an alpha-blended emitter; false when the budget or a kernel is missing.
    [[nodiscard]] bool EnsureSort(Emitter& emitter) noexcept;
    void SortInstances(bgfx::ViewId viewId, const Emitter& emitter, const std::array<float, 4>& cameraPosition) noexcept;

    bgfx::ProgramHandle program_ = BGFX_INVALID_HANDLE;
    // Optional: without it (e.g. no variant for this backend) colliding emitters use the closed-form kernel.
    bgfx::ProgramHandle collideProgram_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle worldUniform_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle worldInverseUniform_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle localUniform_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle planeUniform_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle collisionUniform_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle depthBounceUniform_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle texelUniform_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle viewProjectionUniform_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle depthSampler_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle inverseViewProjectionUniform_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle depthParamsUniform_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle cameraPositionUniform_ = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle fallbackDepth_ = BGFX_INVALID_HANDLE;
    // Optional kernels of the back-to-front sort (all three are needed).
    bgfx::ProgramHandle sortKeysProgram_ = BGFX_INVALID_HANDLE;
    bgfx::ProgramHandle sortStepProgram_ = BGFX_INVALID_HANDLE;
    bgfx::ProgramHandle sortGatherProgram_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle sortCameraUniform_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle sortParamsUniform_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle motionUniform_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle timeUniform_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle colorUniform_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle sizeUniform_ = BGFX_INVALID_HANDLE;
    std::unordered_map<Key, Emitter, KeyHash> emitters_;
    std::unordered_map<std::uint64_t, SceneClock> scenes_;
    std::uint64_t allocatedBytes_ = 0U;
};

} // namespace kb::render
