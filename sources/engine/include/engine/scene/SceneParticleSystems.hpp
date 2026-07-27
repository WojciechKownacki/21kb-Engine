#pragma once

#include "engine/math/EngineMath.hpp"
#include "engine/scene/SceneEntity.hpp"

#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

namespace kb::scene {

class Scene;

// LIB-143: one simulated particle's PHYSICAL state - position/velocity/age/lifetime only.
// Visual attributes (size, color) are deliberately NOT stored here: kb::scene owns
// simulation (does this particle still exist, where is it), kb::render's own rendering-time
// bridge owns appearance (evaluating the SAME ParticleEffectAsset's
// sizeOverLifetime/colorOverLifetime curves against age/lifetime) - mirrors the project's
// established "state belongs to its real owner" rule.
struct ParticleState {
    kb::math::Vec3 position{};
    kb::math::Vec3 velocity{};
    float age = 0.0F;
    float lifetime = 1.0F;
};

struct ParticleSystemFinishedEvent {
    SceneEntity target{};
    std::uint64_t instanceId = 0U;
    std::uint64_t effectAssetId = 0U;
};

// LIB-143: read-only half of SceneParticleSystems, mirroring
// SceneMaterialInstanceQueries' const/mutable split - obtained from a `const Scene&`, safe
// to call from kb::render's per-frame rendering bridge, which never mutates kb::scene state.
class SceneParticleSystemQueries {
public:
    explicit SceneParticleSystemQueries(const Scene& scene) noexcept;

    [[nodiscard]] bool Exists(std::uint64_t id) const noexcept;
    [[nodiscard]] bool IsPlaying(std::uint64_t id) const noexcept;
    // Returns 0 (never a valid asset id) if `id` names no currently live instance.
    [[nodiscard]] std::uint64_t EffectAsset(std::uint64_t id) const noexcept;
    // The particle system's material, resolved ONCE at Create() time from the effect
    // asset's authored `materialReference` - see ParticleEffectAsset.hpp's own doc comment
    // for why resolution cannot happen inside the asset loader itself. Returns 0 for a dead
    // instance.
    [[nodiscard]] std::uint64_t ResolvedMaterialAsset(std::uint64_t id) const noexcept;
    [[nodiscard]] std::uint32_t LiveParticleCount(std::uint64_t id) const noexcept;
    [[nodiscard]] std::span<const ParticleState> Particles(std::uint64_t id) const noexcept;
    // Every currently live particle system instance id in this scene, for kb::render's
    // per-frame rendering bridge to iterate. Small (bounded by
    // kMaxLiveParticleSystemInstances), so returning by value is cheap relative to the GPU
    // work the caller performs per instance.
    [[nodiscard]] std::vector<std::uint64_t> LiveInstanceIds() const;

private:
    const Scene& scene_;
};

// LIB-143: Particles.Create/Play/Stop/SetSeed/SetParameterScalar/Emit's engine-side facade.
// `id` is a monotonically increasing per-scene std::uint64_t (SceneState::
// nextParticleSystemInstanceId), never reused within a scene's lifetime - the same
// convention SceneTimers/SceneMaterialInstances already establish (a stale id can never
// collide with a live one, so no generation-checked handle registry is needed).
//
// A particle system instance always has an `owner` entity (SceneEntity, mirrors
// SceneTimers::Once's owner parameter): particles spawn at the owner's CURRENT world
// position/rotation, re-sampled every emission (an attached effect trails its owner exactly
// like a torch's flame follows the torch), and the instance is auto-released the moment its
// owner is no longer alive or no longer active (Advance()'s OwnerGone check, mirroring
// SceneTimerService's exact convention) - there is no "free-floating, no owner" Create,
// since every meaningful VFX use has a logical owner (even if that owner is an otherwise
// empty marker entity).
class SceneParticleSystems {
public:
    explicit SceneParticleSystems(Scene& scene) noexcept;

    // Returns 0 (never a valid id) if effectAssetId does not resolve to a real
    // ParticleEffect asset, if its authored material reference cannot be resolved to a real
    // RenderMaterial/RenderMaterialInstance asset, or if the scene already holds
    // kMaxLiveParticleSystemInstances live instances.
    // Not noexcept: resolves the effect asset (and its material reference) through
    // AssetManager::Load, which is not itself noexcept (unlike SceneMaterialInstances::
    // Create, which only stores a raw id with no resolution at Create time).
    [[nodiscard]] std::uint64_t Create(std::uint64_t effectAssetId, SceneEntity owner);
    // Idempotent - false if `id` names no currently live instance.
    [[nodiscard]] bool Release(std::uint64_t id) noexcept;
    [[nodiscard]] bool Exists(std::uint64_t id) const noexcept;
    // Play/Stop are "set" operations (mirrors World.SetActive) - true whenever `id` names a
    // live instance, even if it was already in the requested play state. Stop halts NEW
    // emission only; already-live particles keep simulating/dying naturally.
    [[nodiscard]] bool Play(std::uint64_t id) noexcept;
    [[nodiscard]] bool Stop(std::uint64_t id) noexcept;
    [[nodiscard]] bool IsPlaying(std::uint64_t id) const noexcept;
    // Mirrors SceneParticleSystemQueries' own EffectAsset/ResolvedMaterialAsset/Particles -
    // duplicated here (not just on the const Queries half) the same way LiveParticleCount
    // already is, so a native caller holding a non-const Scene& never needs to manufacture a
    // const reference just to read state.
    [[nodiscard]] std::uint64_t EffectAsset(std::uint64_t id) const noexcept;
    [[nodiscard]] std::uint64_t ResolvedMaterialAsset(std::uint64_t id) const noexcept;
    [[nodiscard]] std::span<const ParticleState> Particles(std::uint64_t id) const noexcept;
    // Reseeds the deterministic spawn RNG - affects FUTURE spawns only (already-live
    // particles are never retroactively altered).
    [[nodiscard]] bool SetSeed(std::uint64_t id, std::uint64_t seed) noexcept;
    // Upserts a per-instance override for a NAMED simulation field, overriding the effect
    // asset's own authored value for this instance only. Unlike MaterialInstance's
    // parameters (kb::scene cannot validate those against kb::render's material schema),
    // kb::scene owns particles' ENTIRE simulation schema, so `name` IS validated here: false
    // for a name outside the fixed recognized set (see SceneParticleSystemService.cpp),
    // documented at the script layer's own catalog. Recognized names: emissionRatePerSecond,
    // startSpeedMin, startSpeedMax, startLifetimeMin, startLifetimeMax, spreadDegrees,
    // gravityScale.
    [[nodiscard]] bool SetParameterScalar(std::uint64_t id, std::string_view name, float value) noexcept;
    // Idempotent - false if `id` names no currently live instance, or `name` has no override
    // set (reverts that field to the effect asset's own authored value).
    [[nodiscard]] bool ClearParameter(std::uint64_t id, std::string_view name) noexcept;
    // Immediate, on-demand burst: spawns up to `count` particles right now, independent of
    // Play/Stop state, respecting the instance's current seed stream/parameter overrides.
    // Silently clamps to the remaining capacity under kMaxParticlesPerInstance (mirrors
    // LIB-126's NonAlloc buffer "clamp, do not fail" contract) - false only if `id` names no
    // currently live instance.
    // Not noexcept: resolves the effect asset through AssetManager::Load (see Create's own
    // note).
    [[nodiscard]] bool Emit(std::uint64_t id, std::uint32_t count);
    [[nodiscard]] std::uint32_t LiveParticleCount(std::uint64_t id) const noexcept;
    [[nodiscard]] std::vector<ParticleSystemFinishedEvent> DrainFinishedEvents();

    // Called once per frame by kb::script::ScriptRuntimeSceneSystem with the same
    // scale/pause-aware deltaSeconds Timer/Task already use (SceneTimers::Advance's exact
    // rule) - spawns due particles, integrates live ones (gravity + linear motion), kills
    // particles past their lifetime, and auto-releases any instance whose owner is no longer
    // alive/active.
    void Advance(float deltaSeconds);

private:
    Scene& scene_;
};

} // namespace kb::scene
