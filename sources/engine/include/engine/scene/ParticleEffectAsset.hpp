#pragma once

#include "engine/math/EngineMath.hpp"

#include <cstdint>
#include <string>

namespace kb::scene {

// LIB-143: the payload of a `.kbvfx` ParticleEffect asset - pure CPU simulation data owned
// entirely by kb::scene (kb::scene never depends on kb::render, the same boundary
// MeshRendererComponent::meshAssetId/materialAssetId already respect). `materialReference`
// is the UNRESOLVED authored string (hex asset id or virtual path, exactly like
// RenderMaterialAssetData's own texture path fields) - resolution against AssetRegistry
// happens once, at SceneParticleSystems::Create time (kb::scene has registry access there;
// IAssetLoader::Load does not), mirroring MeshRenderer.SetMaterial's resolve-then-validate
// convention rather than teaching this loader about AssetRegistry lookups.
struct ParticleEffectAsset {
    std::string materialReference;
    bool looping = true;
    // Only meaningful when looping == false: emission stops this many seconds after Play(),
    // already-live particles keep simulating/dying naturally (no forced clear).
    float durationSeconds = 5.0F;
    // Author-facing preference, clamped against the engine's own hard
    // kMaxParticlesPerInstance ceiling at simulation time (SceneParticleSystemService.cpp) -
    // never trusted blindly, the same "hard floor under a configurable value" pattern
    // kMaxLiveMaterialInstances/kMaxLiveTimers already establish.
    std::uint32_t maxParticles = 256U;
    float emissionRatePerSecond = 10.0F;
    float startSpeedMin = 1.0F;
    float startSpeedMax = 2.0F;
    float startLifetimeMin = 1.0F;
    float startLifetimeMax = 1.0F;
    // Local-space base emission direction, rotated by the owning entity's world rotation at
    // spawn time (particles inherit the emitter's facing, not just its position). Default
    // straight up, matching the engine's Y-up convention (EngineMath.hpp).
    kb::math::Vec3 direction{ 0.0F, 1.0F, 0.0F };
    // Half-angle, in degrees, of the random emission cone around `direction`.
    float spreadDegrees = 15.0F;
    float gravityScale = 0.0F;
    // Absolute size (not a multiplier) evaluated at a particle's normalized age [0,1] by
    // kb::render's own rendering-time bridge - see EngineMath.hpp's own doc comment on
    // Curve/Gradient: "nothing in the engine consumes a top-level Curve/Gradient asset yet
    // (no particle system... exists to reference one)" - LIB-143 is that first consumer.
    // An empty curve (author left zero keyframes) evaluates to 0 (invisible) by Curve's own
    // contract, so the loader always fills at least one default keyframe (constant size 1).
    kb::math::Curve sizeOverLifetime;
    // An empty gradient evaluates to Color{} (opaque white) by Gradient's own contract - a
    // sensible default, left as-is when the author provides zero stops.
    kb::math::Gradient colorOverLifetime;
};

} // namespace kb::scene
