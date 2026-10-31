#pragma once

#include <cstdint>
#include <string_view>

namespace kb::render {

enum class ParticleGpuUpdateFailure : std::uint8_t {
    None, InvalidParameters, ResourceCreation, StateAllocation, DiagnosticCapacity, CustomProcess
};

[[nodiscard]] constexpr std::string_view ParticleGpuUpdateFailureName(ParticleGpuUpdateFailure failure) noexcept {
    switch (failure) {
    case ParticleGpuUpdateFailure::CustomProcess: return "custom-process-unavailable";
    case ParticleGpuUpdateFailure::None: return "none";
    case ParticleGpuUpdateFailure::InvalidParameters: return "invalid-parameters";
    case ParticleGpuUpdateFailure::ResourceCreation: return "resource-creation";
    case ParticleGpuUpdateFailure::StateAllocation: return "state-allocation";
    case ParticleGpuUpdateFailure::DiagnosticCapacity: return "diagnostic-capacity";
    }
    return "unknown";
}

} // namespace kb::render
