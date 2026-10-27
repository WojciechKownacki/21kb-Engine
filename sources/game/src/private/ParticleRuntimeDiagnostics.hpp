#pragma once

#include "engine/core/JsonValue.hpp"

#include <cstdint>

namespace kb::scene { class Scene; }
namespace kb::render { class Renderer; }

namespace kb::game {

struct ParticleRuntimeDiagnosticResult {
    kb::core::JsonValue report;
    bool succeeded = false;
    bool gpuExecutionObserved = false;
};

// Terminal capture after EndFrame, before scene release; advances bgfx, never the scene.
[[nodiscard]] ParticleRuntimeDiagnosticResult CaptureParticleRuntimeDiagnostics(
    kb::scene::Scene& scene, kb::render::Renderer& renderer,
    std::uint32_t samples, bool packaged, std::uint32_t renderedFrames,
    std::uint32_t observedCompute, std::uint32_t observedParticleDraws);

} // namespace kb::game
