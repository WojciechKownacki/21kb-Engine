#include "kb/render/particles/ParticleGpuReadback.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace kb::render {

bool ParticleGpuReadback::Initialize() {
    const auto* caps = bgfx::getCaps();
    if (caps == nullptr || bgfx::getRendererType() == bgfx::RendererType::Noop ||
        (caps->supported & BGFX_CAPS_COMPUTE) == 0U) {
        status_ = ParticleGpuReadbackStatus::Unsupported;
        return false;
    }
    status_ = ParticleGpuReadbackStatus::Ready;
    return true;
}

void ParticleGpuReadback::Shutdown() noexcept {
    std::vector<ParticleGpuInstanceRecord>{}.swap(records_);
    std::vector<ParticleGpuInstanceRecord>{}.swap(scratch_);
    status_ = ParticleGpuReadbackStatus::NotInitialized;
}

bool ParticleGpuReadback::Read(const ParticleGpuEmitterSimulation::Draw& draw, std::uint32_t instanceCount) {
    if (instanceCount == 0U || instanceCount > kParticleGpuReadbackMaxInstances ||
        instanceCount > draw.capacity || !bgfx::isValid(draw.instances)) {
        status_ = ParticleGpuReadbackStatus::InvalidRequest;
        return false;
    }
    if (!Initialize()) return false;
    scratch_.resize(instanceCount);
    for (auto& record : scratch_) record.fill(std::numeric_limits<float>::quiet_NaN());
    const bgfx::BufferRegion source{.handle = draw.instances,
        .size = instanceCount * static_cast<std::uint32_t>(sizeof(ParticleGpuInstanceRecord))};
    const auto ready = bgfx::read(source, scratch_.data());
    auto frame = bgfx::frame();
    // The bgfx read contract returns the frame at which the transfer completes (two submitted frames).
    // Keep the CPU storage alive until that frame, even if the device reports an error.
    while (frame < ready) frame = bgfx::frame();
    for (const auto& record : scratch_) {
        if (!std::all_of(record.begin(), record.end(), [](float value) { return std::isfinite(value); })) {
            // A failed device transfer must not turn the sentinel into a fabricated all-dead result.
            status_ = ParticleGpuReadbackStatus::InvalidData;
            return false;
        }
    }
    records_.swap(scratch_);
    status_ = ParticleGpuReadbackStatus::Complete;
    return true;
}

} // namespace kb::render
