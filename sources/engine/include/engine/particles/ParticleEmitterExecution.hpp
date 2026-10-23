#pragma once

#include <cstdint>
#include <string_view>

namespace kb::particles {

enum class ParticleEmitterSimulationPath : std::uint8_t { Cpu, Gpu };

// The first restriction that prevents this emitter from using the GPU kernel.
// Renderer availability is independent of the authored backend policy.
enum class ParticleGpuFallbackReason : std::uint8_t {
    None, CpuPolicy, ConsumerUnavailable, Prewarm, InvalidEmitter, SimulationSpace,
    Output, LocalAlignment, Flipbook, ParticleCapacity, MeshAssets, LocalTrail,
    TrailHistory, LocalPlane, TrailCollision, ContactDeath, TrailField,
    RepeatedNoise, RepeatedAxisRotation, RepeatedAttraction, TorusSwirl, Separation,
    CollisionEvent, WhileAliveEvent, Beam, AbsolutePosition, MeshSpawn, EventMotion,
};

[[nodiscard]] constexpr std::string_view ParticleGpuFallbackReasonName(ParticleGpuFallbackReason reason) noexcept {
    switch (reason) {
    case ParticleGpuFallbackReason::None: return "none";
    case ParticleGpuFallbackReason::CpuPolicy: return "cpu-policy";
    case ParticleGpuFallbackReason::ConsumerUnavailable: return "gpu-consumer-unavailable";
    case ParticleGpuFallbackReason::Prewarm: return "prewarm";
    case ParticleGpuFallbackReason::InvalidEmitter: return "invalid-emitter";
    case ParticleGpuFallbackReason::SimulationSpace: return "simulation-space";
    case ParticleGpuFallbackReason::Output: return "output";
    case ParticleGpuFallbackReason::LocalAlignment: return "local-alignment";
    case ParticleGpuFallbackReason::Flipbook: return "animated-flipbook";
    case ParticleGpuFallbackReason::ParticleCapacity: return "particle-capacity";
    case ParticleGpuFallbackReason::MeshAssets: return "missing-mesh-assets";
    case ParticleGpuFallbackReason::LocalTrail: return "local-trail";
    case ParticleGpuFallbackReason::TrailHistory: return "trail-history-capacity";
    case ParticleGpuFallbackReason::LocalPlane: return "local-plane-collision";
    case ParticleGpuFallbackReason::TrailCollision: return "trail-collision";
    case ParticleGpuFallbackReason::ContactDeath: return "kill-on-contact";
    case ParticleGpuFallbackReason::TrailField: return "trail-force-field";
    case ParticleGpuFallbackReason::RepeatedNoise: return "repeated-noise";
    case ParticleGpuFallbackReason::RepeatedAxisRotation: return "repeated-axis-rotation";
    case ParticleGpuFallbackReason::RepeatedAttraction: return "repeated-attraction";
    case ParticleGpuFallbackReason::TorusSwirl: return "torus-swirl";
    case ParticleGpuFallbackReason::Separation: return "separation";
    case ParticleGpuFallbackReason::CollisionEvent: return "collision-event";
    case ParticleGpuFallbackReason::WhileAliveEvent: return "while-alive-event";
    case ParticleGpuFallbackReason::Beam: return "beam";
    case ParticleGpuFallbackReason::AbsolutePosition: return "absolute-position";
    case ParticleGpuFallbackReason::MeshSpawn: return "mesh-spawn";
    case ParticleGpuFallbackReason::EventMotion: return "event-motion";
    }
    return "unknown";
}

} // namespace kb::particles
