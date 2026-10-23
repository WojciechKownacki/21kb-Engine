#pragma once

#include "kb/render/particles/ParticleGpuEmitterSimulation.hpp"

#include <array>
#include <span>
#include <vector>

namespace kb::render {

inline constexpr std::uint32_t kParticleGpuReadbackMaxInstances = 4096U;
using ParticleGpuInstanceRecord = std::array<float, 20U>;
static_assert(sizeof(ParticleGpuInstanceRecord) == 80U);

enum class ParticleGpuReadbackStatus : std::uint8_t {
    NotInitialized, Ready, Unsupported, InvalidRequest, InvalidData, Complete
};

// Explicit synchronous diagnostics for the actual render-instance buffer. Call after ending the last frame,
// before renderer shutdown. Read advances bgfx frames to complete the transfer, without stepping simulation.
// Keep this outside timing windows. Record interpretation depends on the draw's authored output type.
class ParticleGpuReadback final {
public:
    ParticleGpuReadback() = default;
    ParticleGpuReadback(const ParticleGpuReadback&) = delete;
    ParticleGpuReadback& operator=(const ParticleGpuReadback&) = delete;
    [[nodiscard]] bool Initialize();
    void Shutdown() noexcept;
    [[nodiscard]] bool Read(const ParticleGpuEmitterSimulation::Draw& draw, std::uint32_t instanceCount);
    [[nodiscard]] std::span<const ParticleGpuInstanceRecord> Records() const noexcept { return records_; }
    [[nodiscard]] ParticleGpuReadbackStatus Status() const noexcept { return status_; }

private:
    std::vector<ParticleGpuInstanceRecord> records_, scratch_;
    ParticleGpuReadbackStatus status_ = ParticleGpuReadbackStatus::NotInitialized;
};

} // namespace kb::render
