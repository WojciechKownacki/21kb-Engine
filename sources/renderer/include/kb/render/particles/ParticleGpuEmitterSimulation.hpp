#pragma once

#include "engine/particles/ParticleGpuEmitter.hpp"

#include <bgfx/bgfx.h>

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
    // Rebuilds the instance buffers of the scene emitters; call once per rendered frame.
    void Dispatch(bgfx::ViewId viewId, std::uint64_t sceneId) noexcept;
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
        bgfx::DynamicVertexBufferHandle spawns = BGFX_INVALID_HANDLE;
        bgfx::DynamicVertexBufferHandle instances = BGFX_INVALID_HANDLE;
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

    bgfx::ProgramHandle program_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle motionUniform_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle timeUniform_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle colorUniform_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle sizeUniform_ = BGFX_INVALID_HANDLE;
    std::unordered_map<Key, Emitter, KeyHash> emitters_;
    std::unordered_map<std::uint64_t, SceneClock> scenes_;
    std::uint64_t allocatedBytes_ = 0U;
};

} // namespace kb::render
