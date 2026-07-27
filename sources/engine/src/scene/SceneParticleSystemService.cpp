#include "scene/SceneParticleSystemService.hpp"

#include "engine/assets/AssetId.hpp"
#include "engine/assets/AssetManager.hpp"
#include "engine/assets/AssetMetadata.hpp"
#include "engine/scene/ParticleEffectAsset.hpp"
#include "engine/scene/ParticleEffectAssetIO.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneAssets.hpp"
#include "scene/SceneAccess.hpp"
#include "scene/SceneEntityService.hpp"
#include "scene/SceneRuntimeService.hpp"
#include "scene/SceneState.hpp"
#include "scene/SceneTransformService.hpp"

#include <algorithm>
#include <filesystem>
#include <optional>
#include <utility>

namespace kb::scene {
namespace {

// LIB-143: this is the ticket's own implicit "limit wariantów" analog - a hard cap on how
// many live particle SYSTEM instances a scene may have at once. Smaller than
// kMaxLiveMaterialInstances (512): each instance owns its own particle array (up to
// kMaxParticlesPerInstance below), so the real worst-case memory/CPU cost per instance is
// much higher than a material instance's handful of parameter overrides.
constexpr std::size_t kMaxLiveParticleSystemInstances = 256U;
// Hard ceiling a ParticleEffectAsset's own authored maxParticles is always clamped against -
// never trusted blindly, mirrors kMaxLiveMaterialInstances/kMaxLiveTimers's own "hard floor
// under a configurable value" pattern.
constexpr std::uint32_t kMaxParticlesPerInstance = 2048U;
constexpr float kGravityMetersPerSecondSquared = 9.81F;

// LIB-143: mirrors SceneTimerService::OwnerGone exactly - a particle system instance whose
// owner is no longer alive or no longer active is, from a gameplay standpoint, as "gone" as
// a destroyed one.
[[nodiscard]] bool OwnerGone(const Scene& scene, SceneEntity owner) noexcept {
    return owner.IsValid() && (!SceneEntityService::IsAlive(scene, owner) || !SceneEntityService::IsActive(scene, owner));
}

[[nodiscard]] SceneState::ParticleSystemInstanceRecord* FindLive(SceneState& state, std::uint64_t id) noexcept {
    const auto iterator = std::find_if(state.particleSystems.begin(), state.particleSystems.end(), [id](const SceneState::ParticleSystemInstanceRecord& instance) {
        return instance.id == id;
    });
    return iterator == state.particleSystems.end() ? nullptr : &(*iterator);
}

[[nodiscard]] const SceneState::ParticleSystemInstanceRecord* FindLive(const SceneState& state, std::uint64_t id) noexcept {
    const auto iterator = std::find_if(state.particleSystems.begin(), state.particleSystems.end(), [id](const SceneState::ParticleSystemInstanceRecord& instance) {
        return instance.id == id;
    });
    return iterator == state.particleSystems.end() ? nullptr : &(*iterator);
}

[[nodiscard]] bool IsMaterialAsset(const kb::assets::AssetMetadata& metadata) noexcept {
    return metadata.type == "RenderMaterial" || metadata.type == "RenderMaterialInstance";
}

// Mirrors ScriptMeshRendererApi.cpp's ResolveAssetId exactly (kb::scene cannot depend on
// kb::script, so this is kb::scene's own small copy of the same resolve-by-hex-or-path
// convention, rather than a shared helper across the layer boundary).
[[nodiscard]] kb::assets::AssetId ResolveMaterialReference(kb::assets::AssetManager& manager, std::string_view reference) {
    kb::assets::AssetId id{};
    if (kb::assets::TryParseAssetId(reference, id) && id.IsValid()) {
        const kb::assets::AssetMetadata* metadata = manager.Registry().Find(id);
        return metadata == nullptr || !IsMaterialAsset(*metadata) ? kb::assets::AssetId{} : id;
    }
    const kb::assets::AssetMetadata* metadata = manager.Registry().FindByPath(std::filesystem::path{ reference });
    return metadata == nullptr || !IsMaterialAsset(*metadata) ? kb::assets::AssetId{} : metadata->id;
}

struct ResolvedSimParams {
    float emissionRatePerSecond;
    float startSpeedMin;
    float startSpeedMax;
    float startLifetimeMin;
    float startLifetimeMax;
    kb::math::Vec3 direction;
    float spreadDegrees;
    float gravityScale;
    bool looping;
    float durationSeconds;
    std::uint32_t maxParticles;
};

// Re-resolves the effect asset every call (rather than caching it on the instance record) so
// a hot-reloaded .kbvfx file takes effect immediately - mirrors RuntimeMeshResourceEnsurer's
// own every-frame Load() convention. Per-instance `overrides` (SetParameterScalar) are
// applied on top of the freshly loaded authored values.
[[nodiscard]] std::optional<ResolvedSimParams> ResolveSimParams(Scene& scene, const SceneState::ParticleSystemInstanceRecord& instance) {
    const kb::assets::AssetHandle<ParticleEffectAsset> effect = scene.Assets().Manager().Load<ParticleEffectAsset>(kb::assets::AssetId{ instance.effectAssetId });
    if (!effect.IsLoaded()) {
        return std::nullopt;
    }
    return ResolvedSimParams{
        .emissionRatePerSecond = instance.overrides.emissionRatePerSecond.value_or(effect->emissionRatePerSecond),
        .startSpeedMin = instance.overrides.startSpeedMin.value_or(effect->startSpeedMin),
        .startSpeedMax = instance.overrides.startSpeedMax.value_or(effect->startSpeedMax),
        .startLifetimeMin = instance.overrides.startLifetimeMin.value_or(effect->startLifetimeMin),
        .startLifetimeMax = instance.overrides.startLifetimeMax.value_or(effect->startLifetimeMax),
        .direction = effect->direction,
        .spreadDegrees = instance.overrides.spreadDegrees.value_or(effect->spreadDegrees),
        .gravityScale = instance.overrides.gravityScale.value_or(effect->gravityScale),
        .looping = effect->looping,
        .durationSeconds = effect->durationSeconds,
        .maxParticles = std::min(effect->maxParticles, kMaxParticlesPerInstance),
    };
}

struct RandomConeDirectionResult {
    kb::math::Vec3 direction;
    kb::math::RandomStream stream;
};

// Deterministic random direction within `spreadDegrees` half-angle of `base` (normalized
// internally) - a standard "uniform random point on a spherical cap" construction, built
// entirely on kb::math::RandomStream's own {value, newState} Next* convention (LIB-051).
[[nodiscard]] RandomConeDirectionResult RandomConeDirection(kb::math::Vec3 base, float spreadDegrees, kb::math::RandomStream stream) {
    base = kb::math::Normalize(base);
    if (spreadDegrees <= 0.0F) {
        return RandomConeDirectionResult{ base, stream };
    }
    const kb::math::Vec3 arbitrary = (base.y < 0.99F && base.y > -0.99F) ? kb::math::Vec3{ 0.0F, 1.0F, 0.0F } : kb::math::Vec3{ 1.0F, 0.0F, 0.0F };
    const kb::math::Vec3 right = kb::math::Normalize(kb::math::Cross(arbitrary, base));
    const kb::math::Vec3 up = kb::math::Cross(base, right);

    const kb::math::RandomStreamFloatResult thetaSample = kb::math::NextFloat01(stream);
    const kb::math::RandomStreamFloatResult phiSample = kb::math::NextFloat01(thetaSample.stream);

    const kb::math::Radians theta{ thetaSample.value * 2.0F * kb::math::kPi };
    const kb::math::Radians maxPhi = kb::math::ToRadians(kb::math::Degrees{ spreadDegrees });
    const kb::math::Radians phi{ phiSample.value * maxPhi.Value() };

    const float sinPhi = kb::math::Sin(phi);
    const float direction = kb::math::Cos(phi);
    const kb::math::Vec3 result = (right * (sinPhi * kb::math::Cos(theta))) + (up * (sinPhi * kb::math::Sin(theta))) + (base * direction);
    return RandomConeDirectionResult{ kb::math::Normalize(result), phiSample.stream };
}

void SpawnOne(SceneState::ParticleSystemInstanceRecord& instance, const ResolvedSimParams& params, kb::math::Vec3 originWorldPosition, kb::math::Quat originWorldRotation) {
    if (instance.particles.size() >= params.maxParticles) {
        return;
    }
    const kb::math::RandomStreamRangeResult speedSample = kb::math::NextRange(instance.rng, params.startSpeedMin, params.startSpeedMax);
    const kb::math::RandomStreamRangeResult lifetimeSample = kb::math::NextRange(speedSample.stream, params.startLifetimeMin, params.startLifetimeMax);
    const kb::math::Vec3 worldDirection = kb::math::Rotate(originWorldRotation, params.direction);
    const RandomConeDirectionResult spawnDirection = RandomConeDirection(worldDirection, params.spreadDegrees, lifetimeSample.stream);
    instance.rng = spawnDirection.stream;

    instance.particles.push_back(ParticleState{
        .position = originWorldPosition,
        .velocity = spawnDirection.direction * speedSample.value,
        .age = 0.0F,
        .lifetime = std::max(lifetimeSample.value, 0.0001F),
    });
}

} // namespace

std::uint64_t SceneParticleSystemService::Create(Scene& scene, std::uint64_t effectAssetId, SceneEntity owner) {
    if (effectAssetId == 0U || !owner.IsValid() || !SceneEntityService::IsAlive(scene, owner)) {
        return 0U;
    }
    kb::assets::AssetManager& manager = scene.Assets().Manager();
    const kb::assets::AssetId effectId{ effectAssetId };
    const kb::assets::AssetMetadata* effectMetadata = manager.Registry().Find(effectId);
    if (effectMetadata == nullptr || effectMetadata->type != kParticleEffectAssetType) {
        return 0U;
    }
    const kb::assets::AssetHandle<ParticleEffectAsset> effect = manager.Load<ParticleEffectAsset>(effectId);
    if (!effect.IsLoaded()) {
        return 0U;
    }
    const kb::assets::AssetId materialId = ResolveMaterialReference(manager, effect->materialReference);
    if (!materialId.IsValid()) {
        return 0U;
    }

    SceneState& state = SceneAccess::State(scene);
    if (state.particleSystems.size() >= kMaxLiveParticleSystemInstances) {
        return 0U;
    }
    const std::uint64_t id = state.nextParticleSystemInstanceId++;
    SceneState::ParticleSystemInstanceRecord record{};
    record.id = id;
    record.effectAssetId = effectAssetId;
    record.resolvedMaterialAssetId = materialId.value;
    record.owner = owner;
    record.rng = kb::math::MakeRandomStream(static_cast<std::uint32_t>(id));
    state.particleSystems.push_back(std::move(record));
    return id;
}

bool SceneParticleSystemService::Release(Scene& scene, std::uint64_t id) noexcept {
    SceneState& state = SceneAccess::State(scene);
    const auto iterator = std::find_if(state.particleSystems.begin(), state.particleSystems.end(), [id](const SceneState::ParticleSystemInstanceRecord& instance) {
        return instance.id == id;
    });
    if (iterator == state.particleSystems.end()) {
        return false;
    }
    state.particleSystems.erase(iterator);
    return true;
}

bool SceneParticleSystemService::Exists(const Scene& scene, std::uint64_t id) noexcept {
    return FindLive(SceneAccess::State(scene), id) != nullptr;
}

bool SceneParticleSystemService::Play(Scene& scene, std::uint64_t id) noexcept {
    SceneState::ParticleSystemInstanceRecord* instance = FindLive(SceneAccess::State(scene), id);
    if (instance == nullptr) {
        return false;
    }
    instance->playing = true;
    instance->completionArmed = true;
    instance->elapsedSeconds = 0.0F;
    return true;
}

bool SceneParticleSystemService::Stop(Scene& scene, std::uint64_t id) noexcept {
    SceneState::ParticleSystemInstanceRecord* instance = FindLive(SceneAccess::State(scene), id);
    if (instance == nullptr) {
        return false;
    }
    instance->playing = false;
    return true;
}

bool SceneParticleSystemService::IsPlaying(const Scene& scene, std::uint64_t id) noexcept {
    const SceneState::ParticleSystemInstanceRecord* instance = FindLive(SceneAccess::State(scene), id);
    return instance != nullptr && instance->playing;
}

bool SceneParticleSystemService::SetSeed(Scene& scene, std::uint64_t id, std::uint64_t seed) noexcept {
    SceneState::ParticleSystemInstanceRecord* instance = FindLive(SceneAccess::State(scene), id);
    if (instance == nullptr) {
        return false;
    }
    instance->rng = kb::math::MakeRandomStream(static_cast<std::uint32_t>(seed));
    return true;
}

namespace {

[[nodiscard]] std::optional<float>* FindOverrideField(SceneState::ParticleSystemInstanceRecord& instance, std::string_view name) noexcept {
    if (name == "emissionRatePerSecond") return &instance.overrides.emissionRatePerSecond;
    if (name == "startSpeedMin") return &instance.overrides.startSpeedMin;
    if (name == "startSpeedMax") return &instance.overrides.startSpeedMax;
    if (name == "startLifetimeMin") return &instance.overrides.startLifetimeMin;
    if (name == "startLifetimeMax") return &instance.overrides.startLifetimeMax;
    if (name == "spreadDegrees") return &instance.overrides.spreadDegrees;
    if (name == "gravityScale") return &instance.overrides.gravityScale;
    return nullptr;
}

} // namespace

bool SceneParticleSystemService::SetParameterScalar(Scene& scene, std::uint64_t id, std::string_view name, float value) noexcept {
    SceneState::ParticleSystemInstanceRecord* instance = FindLive(SceneAccess::State(scene), id);
    if (instance == nullptr) {
        return false;
    }
    std::optional<float>* field = FindOverrideField(*instance, name);
    if (field == nullptr) {
        return false;
    }
    *field = value;
    return true;
}

bool SceneParticleSystemService::ClearParameter(Scene& scene, std::uint64_t id, std::string_view name) noexcept {
    SceneState::ParticleSystemInstanceRecord* instance = FindLive(SceneAccess::State(scene), id);
    if (instance == nullptr) {
        return false;
    }
    std::optional<float>* field = FindOverrideField(*instance, name);
    if (field == nullptr || !field->has_value()) {
        return false;
    }
    field->reset();
    return true;
}

bool SceneParticleSystemService::Emit(Scene& scene, std::uint64_t id, std::uint32_t count) {
    SceneState& state = SceneAccess::State(scene);
    SceneState::ParticleSystemInstanceRecord* instance = FindLive(state, id);
    if (instance == nullptr) {
        return false;
    }
    if (count > 0U) {
        instance->completionArmed = true;
    }
    const std::optional<ResolvedSimParams> params = ResolveSimParams(scene, *instance);
    if (!params.has_value()) {
        return true; // handle is live; effect asset transiently unresolvable - honest no-op.
    }
    const TransformComponent* ownerTransform = SceneTransformService::TryGet(scene, instance->owner);
    if (ownerTransform == nullptr) {
        return true;
    }
    for (std::uint32_t spawned = 0U; spawned < count && instance->particles.size() < params->maxParticles; ++spawned) {
        SpawnOne(*instance, *params, ownerTransform->worldPosition, ownerTransform->worldRotation);
    }
    return true;
}

std::uint64_t SceneParticleSystemService::EffectAsset(const Scene& scene, std::uint64_t id) noexcept {
    const SceneState::ParticleSystemInstanceRecord* instance = FindLive(SceneAccess::State(scene), id);
    return instance == nullptr ? 0U : instance->effectAssetId;
}

std::uint64_t SceneParticleSystemService::ResolvedMaterialAsset(const Scene& scene, std::uint64_t id) noexcept {
    const SceneState::ParticleSystemInstanceRecord* instance = FindLive(SceneAccess::State(scene), id);
    return instance == nullptr ? 0U : instance->resolvedMaterialAssetId;
}

std::uint32_t SceneParticleSystemService::LiveParticleCount(const Scene& scene, std::uint64_t id) noexcept {
    const SceneState::ParticleSystemInstanceRecord* instance = FindLive(SceneAccess::State(scene), id);
    return instance == nullptr ? 0U : static_cast<std::uint32_t>(instance->particles.size());
}

std::span<const ParticleState> SceneParticleSystemService::Particles(const Scene& scene, std::uint64_t id) noexcept {
    const SceneState::ParticleSystemInstanceRecord* instance = FindLive(SceneAccess::State(scene), id);
    return instance == nullptr ? std::span<const ParticleState>{} : std::span<const ParticleState>{ instance->particles };
}

std::vector<std::uint64_t> SceneParticleSystemService::LiveInstanceIds(const Scene& scene) {
    const SceneState& state = SceneAccess::State(scene);
    std::vector<std::uint64_t> ids;
    ids.reserve(state.particleSystems.size());
    for (const SceneState::ParticleSystemInstanceRecord& instance : state.particleSystems) {
        ids.push_back(instance.id);
    }
    return ids;
}

std::vector<ParticleSystemFinishedEvent> SceneParticleSystemService::DrainFinishedEvents(Scene& scene) {
    SceneState& state = SceneAccess::State(scene);
    std::vector<ParticleSystemFinishedEvent> events;
    events.swap(state.pendingParticleSystemFinishedEvents);
    return events;
}

void SceneParticleSystemService::Advance(Scene& scene, float deltaSeconds) {
    SceneState& state = SceneAccess::State(scene);
    // LIB-143: identical scale/pause rule to Time.Delta/SceneTimerService::Advance - a
    // particle's notion of elapsed time is always exactly what script code observes through
    // Time.Delta for the same frame.
    const float scale = SceneRuntimeService::IsPlaying(scene) ? SceneRuntimeService::TimeScale(scene) : 0.0F;
    const float effectiveDelta = deltaSeconds * scale;

    std::size_t index = 0U;
    while (index < state.particleSystems.size()) {
        SceneState::ParticleSystemInstanceRecord& instance = state.particleSystems[index];
        if (OwnerGone(scene, instance.owner)) {
            state.particleSystems.erase(state.particleSystems.begin() + static_cast<std::ptrdiff_t>(index));
            continue;
        }

        const std::optional<ResolvedSimParams> params = ResolveSimParams(scene, instance);
        if (!params.has_value()) {
            // Effect asset currently unresolvable (e.g. mid hot-reload) - honest no-op,
            // existing particles hold their last simulated state rather than being guessed
            // at with stale/default physics parameters.
            ++index;
            continue;
        }

        for (ParticleState& particle : instance.particles) {
            particle.velocity.y -= kGravityMetersPerSecondSquared * params->gravityScale * effectiveDelta;
            particle.position = particle.position + particle.velocity * effectiveDelta;
            particle.age += effectiveDelta;
        }
        instance.particles.erase(
            std::remove_if(instance.particles.begin(), instance.particles.end(), [](const ParticleState& particle) {
                return particle.age >= particle.lifetime;
            }),
            instance.particles.end());

        if (instance.playing) {
            instance.elapsedSeconds += effectiveDelta;
            if (!params->looping && instance.elapsedSeconds >= params->durationSeconds) {
                instance.playing = false;
            } else {
                const TransformComponent* ownerTransform = SceneTransformService::TryGet(scene, instance.owner);
                if (ownerTransform != nullptr) {
                    instance.emissionAccumulator += params->emissionRatePerSecond * effectiveDelta;
                    while (instance.emissionAccumulator >= 1.0F && instance.particles.size() < params->maxParticles) {
                        instance.emissionAccumulator -= 1.0F;
                        SpawnOne(instance, *params, ownerTransform->worldPosition, ownerTransform->worldRotation);
                    }
                    // At capacity: do not let the accumulator grow unboundedly, or freeing a
                    // single slot later would burst-spawn the entire backlog at once.
                    instance.emissionAccumulator = std::min(instance.emissionAccumulator, 1.0F);
                }
            }
        }
        if (instance.completionArmed && !instance.playing && instance.particles.empty()) {
            state.pendingParticleSystemFinishedEvents.push_back(ParticleSystemFinishedEvent{
                .target = instance.owner,
                .instanceId = instance.id,
                .effectAssetId = instance.effectAssetId,
            });
            instance.completionArmed = false;
        }
        ++index;
    }
}

} // namespace kb::scene
