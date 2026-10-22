#include "CpuParticleBackend.hpp"
#include "ParticleEffectCompiler.hpp"

#include "engine/assets/AssetId.hpp"
#include "engine/assets/AssetManager.hpp"
#include "engine/assets/AssetMetadata.hpp"
#include "engine/particles/ParticlePlayback.hpp"
#include "engine/scene/ParticleEffectAsset.hpp"
#include "engine/scene/ParticleEffectAssetIO.hpp"
#include "engine/scene/ParticleEffectAssetValidation.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneAssets.hpp"
#include "engine/scene/SceneEntities.hpp"
#include "engine/scene/SceneRuntime.hpp"
#if KB_21KB_PARTICLE_MESH_SHAPE
#include "kb/render/resources/RenderMeshAssetBuilder.hpp"
#endif

#include "engine/particles/ParticlePlayback.hpp"

#include <algorithm>
#include <numeric>
#include <cmath>
#include <cstring>
#include <exception>
#include <limits>

namespace kb::particle_plugin {
namespace {

static_assert(kb::scene::kSceneRuntimeDefaultFixedDeltaSeconds ==
    1.0F / static_cast<float>(kb::scene::kParticleEffectFixedStepsPerSecond));

[[nodiscard]] kb::particles::ParticleRuntimeResult Result(
    kb::particles::ParticleRuntimeStatus status,
    std::uint64_t instanceId = 0U,
    kb::particles::ParticleGpuVisualAvailability gpuVisualAvailability =
        kb::particles::ParticleGpuVisualAvailability::RendererUnavailable) noexcept {
    return { .status = status, .instanceId = instanceId, .gpuVisualAvailability = gpuVisualAvailability };
}

// Turns v about the unit axis by angle radians (Rodrigues).
[[nodiscard]] kb::math::Vec3 RotateAbout(kb::math::Vec3 v, kb::math::Vec3 axis, float angle) noexcept {
    const float c = std::cos(angle), s = std::sin(angle);
    return v * c + kb::math::Cross(axis, v) * s + axis * (kb::math::Dot(axis, v) * (1.0F - c));
}

// Gradient noise with a quintic fade and its exact derivative, and its curl: a divergence-free field
// (Bridson's curl noise), the same construction the other engines' curl noise force uses.
[[nodiscard]] std::uint32_t HashCorner(int x, int y, int z, std::uint32_t seed) noexcept {
    std::uint32_t h = seed * 0x9E3779B9U;
    h ^= static_cast<std::uint32_t>(x) * 0x85EBCA6BU;
    h ^= static_cast<std::uint32_t>(y) * 0xC2B2AE35U;
    h ^= static_cast<std::uint32_t>(z) * 0x27D4EB2FU;
    h ^= h >> 15U; h *= 0x2C1B3C6DU;
    h ^= h >> 12U; h *= 0x297A2D39U;
    h ^= h >> 15U;
    return h;
}
[[nodiscard]] kb::math::Vec3 NoiseGradient(kb::math::Vec3 p, std::uint32_t seed) noexcept {
    static constexpr float kEdges[12][3] = {{1, 1, 0}, {-1, 1, 0}, {1, -1, 0}, {-1, -1, 0}, {1, 0, 1}, {-1, 0, 1},
                                            {1, 0, -1}, {-1, 0, -1}, {0, 1, 1}, {0, -1, 1}, {0, 1, -1}, {0, -1, -1}};
    const float fx = std::floor(p.x), fy = std::floor(p.y), fz = std::floor(p.z);
    const int ix = static_cast<int>(fx), iy = static_cast<int>(fy), iz = static_cast<int>(fz);
    const float f[3] = {p.x - fx, p.y - fy, p.z - fz};
    float u[3], du[3];
    for (int a = 0; a < 3; ++a) {
        u[a] = f[a] * f[a] * f[a] * (f[a] * (f[a] * 6.0F - 15.0F) + 10.0F);
        du[a] = 30.0F * f[a] * f[a] * (f[a] * (f[a] - 2.0F) + 1.0F);
    }
    kb::math::Vec3 g[8];
    float v[8];
    for (int c = 0; c < 8; ++c) {
        const int ox = c & 1, oy = (c >> 1) & 1, oz = (c >> 2) & 1;
        const float* e = kEdges[HashCorner(ix + ox, iy + oy, iz + oz, seed) % 12U];
        g[c] = {e[0], e[1], e[2]};
        v[c] = e[0] * (f[0] - static_cast<float>(ox)) + e[1] * (f[1] - static_cast<float>(oy)) +
               e[2] * (f[2] - static_cast<float>(oz));
    }
    const float k1 = v[1] - v[0], k2 = v[2] - v[0], k3 = v[4] - v[0];
    const float k4 = v[0] - v[1] - v[2] + v[3];
    const float k5 = v[0] - v[2] - v[4] + v[6];
    const float k6 = v[0] - v[1] - v[4] + v[5];
    const float k7 = -v[0] + v[1] + v[2] - v[3] + v[4] - v[5] - v[6] + v[7];
    kb::math::Vec3 d = g[0] + (g[1] - g[0]) * u[0] + (g[2] - g[0]) * u[1] + (g[4] - g[0]) * u[2] +
                       (g[0] - g[1] - g[2] + g[3]) * (u[0] * u[1]) + (g[0] - g[2] - g[4] + g[6]) * (u[1] * u[2]) +
                       (g[0] - g[1] - g[4] + g[5]) * (u[2] * u[0]) +
                       (g[1] + g[2] - g[3] + g[4] - g[5] - g[6] + g[7] - g[0]) * (u[0] * u[1] * u[2]);
    d.x += du[0] * (k1 + u[1] * k4 + u[2] * k6 + u[1] * u[2] * k7);
    d.y += du[1] * (k2 + u[2] * k5 + u[0] * k4 + u[2] * u[0] * k7);
    d.z += du[2] * (k3 + u[0] * k6 + u[1] * k5 + u[0] * u[1] * k7);
    return d;
}
[[nodiscard]] kb::math::Vec3 CurlNoise(kb::math::Vec3 p, std::uint32_t seed) noexcept {
    const kb::math::Vec3 p1 = NoiseGradient(p, seed);
    const kb::math::Vec3 p2 = NoiseGradient(p + kb::math::Vec3{31.416F, -47.853F, 12.793F}, seed + 1U);
    const kb::math::Vec3 p3 = NoiseGradient(p + kb::math::Vec3{-19.123F, 33.301F, -57.456F}, seed + 2U);
    return {p3.y - p2.z, p1.z - p3.x, p2.x - p1.y};
}

[[nodiscard]] kb::math::Vec3 Scale(kb::math::Vec3 value, kb::math::Vec3 scale) noexcept {
    return {value.x * scale.x, value.y * scale.y, value.z * scale.z};
}

[[nodiscard]] kb::math::Vec3 Unscale(kb::math::Vec3 value, kb::math::Vec3 scale) noexcept {
    const auto component = [](float valuePart, float scalePart) noexcept {
        return std::abs(scalePart) > 0.000001F ? valuePart / scalePart : 0.0F;
    };
    return {component(value.x, scale.x), component(value.y, scale.y), component(value.z, scale.z)};
}

[[nodiscard]] kb::math::Vec3 TransformPoint(
    const kb::scene::WorldTransform& transform,
    kb::math::Vec3 point) noexcept {
    return transform.position + kb::math::Rotate(transform.rotation, Scale(point, transform.scale));
}

[[nodiscard]] kb::math::Vec3 InverseTransformPoint(
    const kb::scene::WorldTransform& transform,
    kb::math::Vec3 point) noexcept {
    return Unscale(kb::math::Rotate(kb::math::Inverse(transform.rotation), point - transform.position),
        transform.scale);
}

[[nodiscard]] kb::math::Vec3 TransformDirection(
    const kb::scene::WorldTransform& transform,
    kb::math::Vec3 direction) noexcept {
    return kb::math::Rotate(transform.rotation, direction);
}

[[nodiscard]] bool SameTransform(
    const kb::scene::WorldTransform& lhs,
    const kb::scene::WorldTransform& rhs) noexcept {
    return lhs.position.x == rhs.position.x && lhs.position.y == rhs.position.y &&
        lhs.position.z == rhs.position.z && lhs.rotation.x == rhs.rotation.x &&
        lhs.rotation.y == rhs.rotation.y && lhs.rotation.z == rhs.rotation.z &&
        lhs.rotation.w == rhs.rotation.w && lhs.scale.x == rhs.scale.x &&
        lhs.scale.y == rhs.scale.y && lhs.scale.z == rhs.scale.z;
}

[[nodiscard]] kb::particles::ParticleRenderOutput ToRenderOutput(
    kb::scene::ParticleOutputType output) noexcept {
    using Source = kb::scene::ParticleOutputType;
    using Target = kb::particles::ParticleRenderOutput;
    switch (output) {
    case Source::Billboard: return Target::Billboard;
    case Source::StretchedBillboard: return Target::StretchedBillboard;
    case Source::PointSprite: return Target::PointSprite;
    case Source::Mesh: return Target::Mesh;
    case Source::Trail: return Target::Trail;
    case Source::Ribbon: return Target::Ribbon;
    case Source::Beam: return Target::Beam;
    case Source::Volumetric: return Target::Volumetric;
    case Source::Decal: return Target::Decal;
    }
    std::terminate();
}

[[nodiscard]] kb::particles::ParticleRenderBlendMode ToRenderBlend(
    kb::scene::ParticleBlendMode blend) noexcept {
    using Source = kb::scene::ParticleBlendMode;
    using Target = kb::particles::ParticleRenderBlendMode;
    switch (blend) {
    case Source::Opaque: return Target::Opaque;
    case Source::Alpha: return Target::Alpha;
    case Source::Add: return Target::Add;
    case Source::Multiply: return Target::Multiply;
    case Source::Subtract: return Target::Subtract;
    case Source::Premultiplied: return Target::Premultiplied;
    }
    std::terminate();
}

[[nodiscard]] kb::particles::ParticleRenderSortMode ToRenderSort(
    kb::scene::ParticleSortMode sort) noexcept {
    using Source = kb::scene::ParticleSortMode;
    using Target = kb::particles::ParticleRenderSortMode;
    switch (sort) {
    case Source::None: return Target::None;
    case Source::BackToFront: return Target::BackToFront;
    case Source::FrontToBack: return Target::FrontToBack;
    case Source::Distance: return Target::Distance;
    case Source::Age: return Target::Age;
    }
    std::terminate();
}

[[nodiscard]] kb::particles::ParticleRenderDepthMode ToRenderDepth(
    bool depthTest,
    bool depthWrite) noexcept {
    if (depthTest) return depthWrite
        ? kb::particles::ParticleRenderDepthMode::ReadWrite
        : kb::particles::ParticleRenderDepthMode::ReadOnly;
    return depthWrite
        ? kb::particles::ParticleRenderDepthMode::WriteOnly
        : kb::particles::ParticleRenderDepthMode::Disabled;
}

[[nodiscard]] std::uint32_t PackColor(const kb::math::Color& color) noexcept {
    const auto channel = [](float value) noexcept {
        return static_cast<std::uint32_t>(std::lround(kb::math::Clamp(value, 0.0F, 1.0F) * 255.0F));
    };
    return channel(color.r) | (channel(color.g) << 8U) |
        (channel(color.b) << 16U) | (channel(color.a) << 24U);
}

[[nodiscard]] std::array<std::int16_t, 4U> PackQuaternion(kb::math::Quat value) noexcept {
    value = kb::math::Normalize(value);
    const auto pack = [](float component) noexcept {
        return static_cast<std::int16_t>(std::lround(
            kb::math::Clamp(component, -1.0F, 1.0F) * 32767.0F));
    };
    return {pack(value.x), pack(value.y), pack(value.z), pack(value.w)};
}

[[nodiscard]] kb::particles::ParticleRenderAlignment ToRenderAlignment(
    kb::scene::ParticleAlignment alignment) noexcept {
    using Source = kb::scene::ParticleAlignment;
    using Target = kb::particles::ParticleRenderAlignment;
    switch (alignment) {
    case Source::CameraFacing: return Target::CameraFacing;
    case Source::Velocity: return Target::Velocity;
    case Source::WorldUp: return Target::WorldUp;
    case Source::Local: return Target::Local;
    }
    std::terminate();
}

[[nodiscard]] constexpr std::size_t RenderGroupIndex(
    std::uint32_t denseIndex,
    std::uint8_t emitterIndex) noexcept {
    return static_cast<std::size_t>(denseIndex) * kb::scene::kParticleEffectMaxEmitters + emitterIndex;
}

} // namespace

void CpuParticleBackend::Warmup() {
    if (warmedUp_) return;
    slotToDense_.fill(kInvalidDenseIndex);
    slotGenerations_.fill(1U);
    denseToSlot_.resize(kb::scene::kParticleEffectMaxInstancesPerScene);
    effectAssetIds_.resize(kb::scene::kParticleEffectMaxInstancesPerScene);
    owners_.resize(kb::scene::kParticleEffectMaxInstancesPerScene);
    ownerDeathPolicies_.resize(kb::scene::kParticleEffectMaxInstancesPerScene,
        kb::scene::ParticleOwnerDeathPolicy::Drain);
    ownerTerminalPending_.resize(kb::scene::kParticleEffectMaxInstancesPerScene);
    rateMultipliers_.resize(kb::scene::kParticleEffectMaxInstancesPerScene, 1.0F);
    maxParticlesOverrides_.resize(kb::scene::kParticleEffectMaxInstancesPerScene);
    followTransforms_.resize(kb::scene::kParticleEffectMaxInstancesPerScene);
    ownerTransforms_.resize(kb::scene::kParticleEffectMaxInstancesPerScene);
    reloadRestarted_.resize(kb::scene::kParticleEffectMaxInstancesPerScene);
    executionPaths_.resize(kb::scene::kParticleEffectMaxInstancesPerScene);
    gpuVisualAvailability_.resize(kb::scene::kParticleEffectMaxInstancesPerScene);
    gpuVisualCapabilityEpochs_.resize(kb::scene::kParticleEffectMaxInstancesPerScene);
    gpuVisualRestartRequired_.resize(kb::scene::kParticleEffectMaxInstancesPerScene);
    seeds_.resize(kb::scene::kParticleEffectMaxInstancesPerScene);
    playbackStates_.resize(kb::scene::kParticleEffectMaxInstancesPerScene);
    instanceRuntime_.resize(kb::scene::kParticleEffectMaxInstancesPerScene);
    compiledEffects_.resize(kb::scene::kParticleEffectMaxInstancesPerScene);
    parameters_.reserve(kb::scene::kParticleEffectMaxInstancesPerScene *
                        kb::scene::kParticleEffectMaxRuntimeParametersPerInstance);
    commands_.reserve(kb::scene::kParticleEffectMaxCommandsPerStep);
    currentEvents_.reserve(kb::scene::kParticleEffectMaxEventsPerStep);
    nextEvents_.reserve(kb::scene::kParticleEffectMaxEventsPerStep);
    prewarmCurrentEvents_.reserve(kb::scene::kParticleEffectMaxEventsPerStep);
    prewarmNextEvents_.reserve(kb::scene::kParticleEffectMaxEventsPerStep);
    particleInstanceIds_.reserve(kb::scene::kParticleEffectMaxCpuParticlesPerScene);
    particleIds_.reserve(kb::scene::kParticleEffectMaxCpuParticlesPerScene);
    particleSpawnOrdinals_.reserve(kb::scene::kParticleEffectMaxCpuParticlesPerScene);
    particleRibbonGroups_.reserve(kb::scene::kParticleEffectMaxCpuParticlesPerScene);
    particleEmitterIndices_.reserve(kb::scene::kParticleEffectMaxCpuParticlesPerScene);
    particlePositions_.reserve(kb::scene::kParticleEffectMaxCpuParticlesPerScene);
    particlePreviousPositions_.reserve(kb::scene::kParticleEffectMaxCpuParticlesPerScene);
    particleVelocities_.reserve(kb::scene::kParticleEffectMaxCpuParticlesPerScene);
    particleAges_.reserve(kb::scene::kParticleEffectMaxCpuParticlesPerScene);
    particleLifetimes_.reserve(kb::scene::kParticleEffectMaxCpuParticlesPerScene);
    particleColors_.reserve(kb::scene::kParticleEffectMaxCpuParticlesPerScene);
    particleSizes_.reserve(kb::scene::kParticleEffectMaxCpuParticlesPerScene);
    particleSpins_.reserve(kb::scene::kParticleEffectMaxCpuParticlesPerScene);
    particleSpinRates_.reserve(kb::scene::kParticleEffectMaxCpuParticlesPerScene);
    particleSizeScales_.reserve(kb::scene::kParticleEffectMaxCpuParticlesPerScene);
    particleEventDepths_.reserve(kb::scene::kParticleEffectMaxCpuParticlesPerScene);
    particlePrewarmGroups_.reserve(kb::scene::kParticleEffectMaxCpuParticlesPerScene);
    renderEmitterScratch_.resize(kb::particles::kParticleRenderSnapshotMaxEmitterRecords);
    renderParticleScratch_.reserve(4'096U);
    constexpr std::size_t renderGroupCount = kb::scene::kParticleEffectMaxInstancesPerScene *
        kb::scene::kParticleEffectMaxEmitters;
    renderGroupCounts_.resize(renderGroupCount);
    renderGroupWriteOffsets_.resize(renderGroupCount);
    renderGroupRecordIndices_.resize(renderGroupCount);
    renderRejectedByCapacity_.resize(renderGroupCount);
    renderRejectedBySpawnBudget_.resize(renderGroupCount);
    renderRejectedByEventBudget_.resize(renderGroupCount);
    collisionEventsThisStep_.resize(renderGroupCount * kb::scene::kParticleEffectMaxModulesPerEmitter);
    warmedUp_ = true;
}

bool CpuParticleBackend::IsWarmedUp() const noexcept { return warmedUp_; }
std::size_t CpuParticleBackend::LiveInstanceCount() const noexcept { return denseInstanceCount_; }
std::size_t CpuParticleBackend::ParticleCapacity() const noexcept { return particlePositions_.capacity(); }
std::size_t CpuParticleBackend::BufferedCommandCount() const noexcept { return commands_.size(); }
std::size_t CpuParticleBackend::BufferedEventCount() const noexcept {
    return currentEvents_.size() + nextEvents_.size() +
        prewarmCurrentEvents_.size() + prewarmNextEvents_.size();
}
CpuParticleBackend::StepTelemetry CpuParticleBackend::LastStepTelemetry() const noexcept { return stepTelemetry_; }
std::size_t CpuParticleBackend::CompiledEffectCount() const noexcept {
    return static_cast<std::size_t>(std::count_if(compiledEffects_.begin(), compiledEffects_.end(),
        [](const CompiledEffectEntry& effect) { return effect.referenceCount != 0U; }));
}

kb::particles::ParticleRuntimeResult CpuParticleBackend::ConfigureOwnerDeathPolicy(
    std::uint64_t instanceId,
    kb::scene::ParticleOwnerDeathPolicy policy) noexcept {
    const std::uint32_t denseIndex = ResolveDenseIndex(instanceId);
    if (denseIndex == kInvalidDenseIndex) {
        return Result(kb::particles::ParticleRuntimeStatus::InvalidInstance, instanceId);
    }
    if (policy != kb::scene::ParticleOwnerDeathPolicy::Drain &&
        policy != kb::scene::ParticleOwnerDeathPolicy::Clear) {
        return Result(kb::particles::ParticleRuntimeStatus::InvalidParameter, instanceId);
    }
    ownerDeathPolicies_[denseIndex] = policy;
    return Result(kb::particles::ParticleRuntimeStatus::Success, instanceId);
}

kb::particles::ParticleRuntimeResult CpuParticleBackend::ConfigureComponent(
    kb::scene::Scene&,
    std::uint64_t instanceId,
    float rateMultiplier,
    std::uint32_t maxParticlesOverride,
    bool followTransform,
    const kb::scene::WorldTransform& ownerTransform) noexcept {
    const std::uint32_t denseIndex = ResolveDenseIndex(instanceId);
    if (denseIndex == kInvalidDenseIndex) {
        return Result(kb::particles::ParticleRuntimeStatus::InvalidInstance, instanceId);
    }
    if (!std::isfinite(rateMultiplier) || rateMultiplier <= 0.0F || rateMultiplier > 1024.0F ||
        maxParticlesOverride > kb::scene::kParticleEffectMaxCpuParticlesPerEmitter) {
        return Result(kb::particles::ParticleRuntimeStatus::InvalidParameter, instanceId);
    }
    const bool wasConfigured = followTransforms_[denseIndex] != 0U;
    const bool wasFollowing = followTransforms_[denseIndex] == 1U;
    const kb::scene::WorldTransform previous = ownerTransforms_[denseIndex];
    if (followTransform) {
        if (!SameTransform(previous, ownerTransform)) {
            ApplyOwnerTransformDelta(denseIndex, previous, ownerTransform);
        }
        ownerTransforms_[denseIndex] = ownerTransform;
    } else if (!wasConfigured || wasFollowing) {
        ownerTransforms_[denseIndex] = ownerTransform;
    }
    rateMultipliers_[denseIndex] = rateMultiplier;
    maxParticlesOverrides_[denseIndex] = maxParticlesOverride;
    followTransforms_[denseIndex] = followTransform ? 1U : 2U;
    return Result(kb::particles::ParticleRuntimeStatus::Success, instanceId);
}

std::uint64_t CpuParticleBackend::MakeInstanceId(std::uint32_t slot, std::uint32_t generation) noexcept {
    return (static_cast<std::uint64_t>(generation) << 32U) | (static_cast<std::uint64_t>(slot) + 1U);
}

std::uint32_t CpuParticleBackend::ResolveDenseIndex(std::uint64_t instanceId) const noexcept {
    const std::uint32_t encodedSlot = static_cast<std::uint32_t>(instanceId);
    const std::uint32_t generation = static_cast<std::uint32_t>(instanceId >> 32U);
    if (encodedSlot == 0U || generation == 0U) return kInvalidDenseIndex;
    const std::uint32_t slot = encodedSlot - 1U;
    if (slot >= slotToDense_.size() || slotGenerations_[slot] != generation) return kInvalidDenseIndex;
    const std::uint32_t denseIndex = slotToDense_[slot];
    return denseIndex < denseInstanceCount_ && denseToSlot_[denseIndex] == slot ? denseIndex : kInvalidDenseIndex;
}

kb::particles::ParticleRuntimeResult CpuParticleBackend::Create(
    kb::scene::Scene& scene,
    std::uint64_t effectAssetId,
    kb::scene::SceneEntity owner) {
    if (!warmedUp_) return Result(kb::particles::ParticleRuntimeStatus::InvalidRequest);
    const kb::assets::AssetMetadata* metadata =
        scene.Assets().Manager().Registry().Find(kb::assets::AssetId{ effectAssetId });
    if (metadata == nullptr || metadata->type != kb::scene::kParticleEffectAssetType) {
        return Result(kb::particles::ParticleRuntimeStatus::InvalidAsset);
    }
    if (!owner.IsValid() || !scene.Entities().IsAlive(owner)) {
        return Result(kb::particles::ParticleRuntimeStatus::InvalidOwner);
    }
    if (denseInstanceCount_ >= kb::scene::kParticleEffectMaxInstancesPerScene) {
        return Result(kb::particles::ParticleRuntimeStatus::InstanceLimitReached);
    }

    kb::particles::ParticleRuntimeStatus compileFailure = kb::particles::ParticleRuntimeStatus::InvalidAsset;
    const std::uint32_t compiledEffectIndex = AcquireCompiledEffect(scene, effectAssetId, compileFailure);
    if (compiledEffectIndex == kInvalidDenseIndex) {
        return Result(compileFailure);
    }
    const kb::particles::ParticleRenderCapabilities renderCapabilities =
        kb::particles::ParticlePlayback::RenderCapabilities(scene);
    const kb::particles::ParticleGpuVisualAvailability gpuVisualAvailability =
        renderCapabilities.gpuVisualAvailability;
    if (compiledEffects_[compiledEffectIndex].effect->backendPolicy ==
            kb::scene::ParticleBackendPolicy::GpuVisualRequired &&
        gpuVisualAvailability != kb::particles::ParticleGpuVisualAvailability::Ready) {
        ReleaseCompiledEffect(compiledEffectIndex);
        return Result(kb::particles::ParticleRuntimeStatus::BackendUnavailable, 0U, gpuVisualAvailability);
    }

    std::uint32_t slot = 0U;
    while (slot < slotToDense_.size() && slotToDense_[slot] != kInvalidDenseIndex) ++slot;
    if (slot == slotToDense_.size()) {
        ReleaseCompiledEffect(compiledEffectIndex);
        return Result(kb::particles::ParticleRuntimeStatus::InstanceLimitReached);
    }

    const std::uint32_t denseIndex = denseInstanceCount_++;
    slotToDense_[slot] = denseIndex;
    denseToSlot_[denseIndex] = slot;
    effectAssetIds_[denseIndex] = effectAssetId;
    owners_[denseIndex] = owner;
    ownerDeathPolicies_[denseIndex] = kb::scene::ParticleOwnerDeathPolicy::Drain;
    ownerTerminalPending_[denseIndex] = 0U;
    rateMultipliers_[denseIndex] = 1.0F;
    maxParticlesOverrides_[denseIndex] = 0U;
    followTransforms_[denseIndex] = 0U;
    ownerTransforms_[denseIndex] = {};
    reloadRestarted_[denseIndex] = 0U;
    const kb::scene::ParticleBackendPolicy backendPolicy =
        compiledEffects_[compiledEffectIndex].effect->backendPolicy;
    executionPaths_[denseIndex] = gpuVisualAvailability ==
            kb::particles::ParticleGpuVisualAvailability::Ready &&
            backendPolicy != kb::scene::ParticleBackendPolicy::CpuDeterministic
        ? kb::particles::ParticleRuntimeExecutionPath::GpuVisual
        : (backendPolicy == kb::scene::ParticleBackendPolicy::GpuVisualPreferred
            ? kb::particles::ParticleRuntimeExecutionPath::CpuFallback
            : kb::particles::ParticleRuntimeExecutionPath::CpuDeterministic);
    gpuVisualAvailability_[denseIndex] = gpuVisualAvailability;
    gpuVisualCapabilityEpochs_[denseIndex] = renderCapabilities.capabilityEpoch;
    gpuVisualRestartRequired_[denseIndex] = 0U;
    seeds_[denseIndex] = compiledEffects_[compiledEffectIndex].effect->determinismSeed;
    playbackStates_[denseIndex] = PlaybackState::Stopped;
    const std::uint64_t instanceId = MakeInstanceId(slot, slotGenerations_[slot]);
    instanceRuntime_[denseIndex] = {};
    instanceRuntime_[denseIndex].compiledEffectIndex = compiledEffectIndex;
    ResetInstance(denseIndex);
    return Result(kb::particles::ParticleRuntimeStatus::Success, instanceId);
}

kb::particles::ParticleRuntimeResult CpuParticleBackend::Submit(Command command) noexcept {
    if (!warmedUp_) return Result(kb::particles::ParticleRuntimeStatus::InvalidRequest, command.instanceId);
    if (commands_.size() >= commands_.capacity()) {
        return Result(kb::particles::ParticleRuntimeStatus::InvalidRequest, command.instanceId);
    }
    commands_.push_back(command);
    const kb::particles::ParticleRuntimeResult result = Execute(commands_.back());
    commands_.pop_back();
    return result;
}

kb::particles::ParticleRuntimeResult CpuParticleBackend::Execute(const Command& command) noexcept {
    const std::uint32_t denseIndex = ResolveDenseIndex(command.instanceId);
    if (denseIndex == kInvalidDenseIndex) {
        return Result(kb::particles::ParticleRuntimeStatus::InvalidInstance, command.instanceId);
    }

    const auto parameterNameEquals = [](const RuntimeParameter& parameter, std::string_view name) noexcept {
        return parameter.nameLength == name.size() &&
               std::memcmp(parameter.name.data(), name.data(), name.size()) == 0;
    };

    switch (command.type) {
    case CommandType::Release:
        QueueGpuRelease(command.instanceId, denseIndex, true);
        RemoveParticles(command.instanceId);
        RemoveQueuedEvents(command.instanceId);
        std::erase_if(parameters_, [&](const ParameterEntry& entry) { return entry.instanceId == command.instanceId; });
        ReleaseCompiledEffect(instanceRuntime_[denseIndex].compiledEffectIndex);
        RemoveDenseInstance(denseIndex);
        return Result(kb::particles::ParticleRuntimeStatus::Success, command.instanceId);
    case CommandType::Play:
        if (playbackStates_[denseIndex] == PlaybackState::Stopped) {
            ResetInstance(denseIndex);
            playbackStates_[denseIndex] = PlaybackState::Playing;
            const kb::particles::ParticleRuntimeStatus status = PrewarmInstance(denseIndex);
            if (status != kb::particles::ParticleRuntimeStatus::Success) {
                RemoveParticles(command.instanceId);
                ResetInstance(denseIndex);
                playbackStates_[denseIndex] = PlaybackState::Stopped;
            }
            return Result(status, command.instanceId);
        }
        playbackStates_[denseIndex] = PlaybackState::Playing;
        return Result(kb::particles::ParticleRuntimeStatus::Success, command.instanceId);
    case CommandType::Pause:
        if (playbackStates_[denseIndex] != PlaybackState::Playing) {
            return Result(kb::particles::ParticleRuntimeStatus::InvalidRequest, command.instanceId);
        }
        playbackStates_[denseIndex] = PlaybackState::Paused;
        return Result(kb::particles::ParticleRuntimeStatus::Success, command.instanceId);
    case CommandType::Stop:
        playbackStates_[denseIndex] = LiveParticleCount(command.instanceId) == 0U
            ? PlaybackState::Stopped : PlaybackState::Draining;
        return Result(kb::particles::ParticleRuntimeStatus::Success, command.instanceId);
    case CommandType::Restart:
        QueueGpuRelease(command.instanceId, denseIndex, false);
        RemoveParticles(command.instanceId);
        RemoveQueuedEvents(command.instanceId);
        ResetInstance(denseIndex);
        playbackStates_[denseIndex] = PlaybackState::Playing;
        {
            const kb::particles::ParticleRuntimeStatus status = PrewarmInstance(denseIndex);
            if (status != kb::particles::ParticleRuntimeStatus::Success) {
                RemoveParticles(command.instanceId);
                ResetInstance(denseIndex);
                playbackStates_[denseIndex] = PlaybackState::Stopped;
            }
            return Result(status, command.instanceId);
        }
    case CommandType::SetSeed:
        seeds_[denseIndex] = command.seed;
        instanceRuntime_[denseIndex].randomState = command.seed ^
            (command.instanceId * 0x9E3779B97F4A7C15ULL);
        return Result(kb::particles::ParticleRuntimeStatus::Success, command.instanceId);
    case CommandType::SetParameter: {
        std::size_t count = 0U;
        for (ParameterEntry& entry : parameters_) {
            if (entry.instanceId != command.instanceId) continue;
            ++count;
            if (parameterNameEquals(entry.parameter,
                    std::string_view{ command.parameter.name.data(), command.parameter.nameLength })) {
                entry.parameter.value = command.parameter.value;
                return Result(kb::particles::ParticleRuntimeStatus::Success, command.instanceId);
            }
        }
        if (count >= kb::scene::kParticleEffectMaxRuntimeParametersPerInstance) {
            return Result(kb::particles::ParticleRuntimeStatus::InvalidParameter, command.instanceId);
        }
        parameters_.push_back({ .instanceId = command.instanceId, .parameter = command.parameter });
        return Result(kb::particles::ParticleRuntimeStatus::Success, command.instanceId);
    }
    case CommandType::ClearParameter: {
        const std::string_view name{ command.parameter.name.data(), command.parameter.nameLength };
        for (std::size_t index = 0U; index < parameters_.size(); ++index) {
            if (parameters_[index].instanceId != command.instanceId ||
                !parameterNameEquals(parameters_[index].parameter, name)) continue;
            parameters_[index] = parameters_.back();
            parameters_.pop_back();
            return Result(kb::particles::ParticleRuntimeStatus::Success, command.instanceId);
        }
        return Result(kb::particles::ParticleRuntimeStatus::InvalidParameter, command.instanceId);
    }
    }
    return Result(kb::particles::ParticleRuntimeStatus::InvalidRequest, command.instanceId);
}

kb::particles::ParticleRuntimeResult CpuParticleBackend::Release(
    kb::scene::Scene&,
    std::uint64_t instanceId) noexcept {
    return Submit({ .type = CommandType::Release, .instanceId = instanceId });
}
kb::particles::ParticleRuntimeResult CpuParticleBackend::Play(
    kb::scene::Scene& scene,
    std::uint64_t instanceId) noexcept {
    const std::uint32_t denseIndex = ResolveDenseIndex(instanceId);
    if (denseIndex == kInvalidDenseIndex) {
        return Result(kb::particles::ParticleRuntimeStatus::InvalidInstance, instanceId);
    }
    if (compiledEffects_[instanceRuntime_[denseIndex].compiledEffectIndex].effect->backendPolicy ==
            kb::scene::ParticleBackendPolicy::GpuVisualRequired) {
        const kb::particles::ParticleGpuVisualAvailability availability =
            kb::particles::ParticlePlayback::RenderCapabilities(scene).gpuVisualAvailability;
        if (availability != kb::particles::ParticleGpuVisualAvailability::Ready) {
            return Result(kb::particles::ParticleRuntimeStatus::BackendUnavailable, instanceId, availability);
        }
    }
    return Submit({ .type = CommandType::Play, .instanceId = instanceId });
}
kb::particles::ParticleRuntimeResult CpuParticleBackend::Pause(
    kb::scene::Scene&,
    std::uint64_t instanceId) noexcept {
    return Submit({ .type = CommandType::Pause, .instanceId = instanceId });
}
kb::particles::ParticleRuntimeResult CpuParticleBackend::Stop(
    kb::scene::Scene&,
    std::uint64_t instanceId) noexcept {
    return Submit({ .type = CommandType::Stop, .instanceId = instanceId });
}
kb::particles::ParticleRuntimeResult CpuParticleBackend::Restart(
    kb::scene::Scene& scene,
    std::uint64_t instanceId) noexcept {
    const std::uint32_t denseIndex = ResolveDenseIndex(instanceId);
    if (denseIndex == kInvalidDenseIndex) {
        return Result(kb::particles::ParticleRuntimeStatus::InvalidInstance, instanceId);
    }
    const kb::scene::ParticleBackendPolicy backendPolicy =
        compiledEffects_[instanceRuntime_[denseIndex].compiledEffectIndex].effect->backendPolicy;
    const kb::particles::ParticleRenderCapabilities renderCapabilities =
        kb::particles::ParticlePlayback::RenderCapabilities(scene);
    if (backendPolicy == kb::scene::ParticleBackendPolicy::GpuVisualRequired &&
        renderCapabilities.gpuVisualAvailability != kb::particles::ParticleGpuVisualAvailability::Ready) {
        return Result(kb::particles::ParticleRuntimeStatus::BackendUnavailable, instanceId,
            renderCapabilities.gpuVisualAvailability);
    }
    const kb::particles::ParticleRuntimeResult result =
        Submit({ .type = CommandType::Restart, .instanceId = instanceId });
    if (!result.Succeeded() || backendPolicy == kb::scene::ParticleBackendPolicy::CpuDeterministic) {
        return result;
    }
    executionPaths_[denseIndex] = renderCapabilities.gpuVisualAvailability ==
            kb::particles::ParticleGpuVisualAvailability::Ready
        ? kb::particles::ParticleRuntimeExecutionPath::GpuVisual
        : kb::particles::ParticleRuntimeExecutionPath::CpuFallback;
    gpuVisualAvailability_[denseIndex] = renderCapabilities.gpuVisualAvailability;
    gpuVisualCapabilityEpochs_[denseIndex] = renderCapabilities.capabilityEpoch;
    gpuVisualRestartRequired_[denseIndex] = 0U;
    return result;
}
kb::particles::ParticleRuntimeResult CpuParticleBackend::SetSeed(
    kb::scene::Scene&,
    std::uint64_t instanceId,
    std::uint64_t seed) noexcept {
    return Submit({ .type = CommandType::SetSeed, .instanceId = instanceId, .seed = seed });
}

kb::particles::ParticleRuntimeResult CpuParticleBackend::SetParameterScalar(
    kb::scene::Scene&,
    std::uint64_t instanceId,
    std::string_view name,
    float value) noexcept {
    if (name.empty() || name.size() >= kb::scene::kParticleEffectMaxRuntimeParameterNameBytes || !std::isfinite(value)) {
        return Result(kb::particles::ParticleRuntimeStatus::InvalidParameter, instanceId);
    }
    Command command{ .type = CommandType::SetParameter, .instanceId = instanceId };
    command.parameter.nameLength = static_cast<std::uint16_t>(name.size());
    std::memcpy(command.parameter.name.data(), name.data(), name.size());
    command.parameter.value = value;
    return Submit(command);
}

kb::particles::ParticleRuntimeResult CpuParticleBackend::ClearParameter(
    kb::scene::Scene&,
    std::uint64_t instanceId,
    std::string_view name) noexcept {
    if (name.empty() || name.size() >= kb::scene::kParticleEffectMaxRuntimeParameterNameBytes) {
        return Result(kb::particles::ParticleRuntimeStatus::InvalidParameter, instanceId);
    }
    Command command{ .type = CommandType::ClearParameter, .instanceId = instanceId };
    command.parameter.nameLength = static_cast<std::uint16_t>(name.size());
    std::memcpy(command.parameter.name.data(), name.data(), name.size());
    return Submit(command);
}

kb::particles::ParticleRuntimeResult CpuParticleBackend::Emit(
    kb::scene::Scene&,
    std::uint64_t instanceId,
    std::uint32_t count) {
    const std::uint32_t denseIndex = ResolveDenseIndex(instanceId);
    if (denseIndex == kInvalidDenseIndex) {
        return Result(kb::particles::ParticleRuntimeStatus::InvalidInstance, instanceId);
    }
    if (count == 0U) return Result(kb::particles::ParticleRuntimeStatus::InvalidRequest, instanceId);
    stepTelemetry_ = {};
    std::fill(renderRejectedByCapacity_.begin(), renderRejectedByCapacity_.end(), 0U);
    std::fill(renderRejectedBySpawnBudget_.begin(), renderRejectedBySpawnBudget_.end(), 0U);
    std::fill(renderRejectedByEventBudget_.begin(), renderRejectedByEventBudget_.end(), 0U);
    eventQueueOverflowed_ = false;
    eventActionBudgetExceeded_ = false;
    stepTelemetry_.requestedSpawns = count;
    if (count > kb::scene::kParticleEffectMaxSpawnsPerStep) {
        stepTelemetry_.rejectedByStepBudget = count;
        renderRejectedBySpawnBudget_[RenderGroupIndex(denseIndex, 0U)] = count;
        return Result(kb::particles::ParticleRuntimeStatus::SpawnBudgetExceeded, instanceId);
    }
    if (!SpawnExact(denseIndex, 0U, count)) {
        if (eventQueueOverflowed_) {
            return Result(kb::particles::ParticleRuntimeStatus::EventQueueFull, instanceId);
        }
        stepTelemetry_.rejectedByCapacity = count;
        renderRejectedByCapacity_[RenderGroupIndex(denseIndex, 0U)] = count;
        return Result(kb::particles::ParticleRuntimeStatus::ParticleCapacityReached, instanceId);
    }
    stepTelemetry_.spawned = count;
    return Result(kb::particles::ParticleRuntimeStatus::Success, instanceId);
}

kb::particles::ParticleRuntimeResult CpuParticleBackend::Step(
    kb::scene::Scene& scene,
    float fixedDeltaSeconds) {
    if (!warmedUp_ || !std::isfinite(fixedDeltaSeconds) ||
        std::abs(fixedDeltaSeconds - kb::scene::kSceneRuntimeDefaultFixedDeltaSeconds) > 0.0000001F) {
        return Result(kb::particles::ParticleRuntimeStatus::InvalidRequest);
    }
    FollowSimulationOrigin(kb::particles::ParticlePlayback::SimulationOrigin(scene));
    for (std::uint32_t denseIndex = 0U; denseIndex < denseInstanceCount_; ++denseIndex) {
        reloadRestarted_[denseIndex] = 0U;
    }
    const kb::particles::ParticleRenderCapabilities renderCapabilities =
        kb::particles::ParticlePlayback::RenderCapabilities(scene);
    for (std::uint32_t denseIndex = 0U; denseIndex < denseInstanceCount_; ++denseIndex) {
        const kb::scene::ParticleBackendPolicy backendPolicy =
            compiledEffects_[instanceRuntime_[denseIndex].compiledEffectIndex].effect->backendPolicy;
        if (backendPolicy == kb::scene::ParticleBackendPolicy::CpuDeterministic ||
            gpuVisualCapabilityEpochs_[denseIndex] == renderCapabilities.capabilityEpoch) {
            continue;
        }
        gpuVisualCapabilityEpochs_[denseIndex] = renderCapabilities.capabilityEpoch;
        gpuVisualAvailability_[denseIndex] = renderCapabilities.gpuVisualAvailability;
        gpuVisualRestartRequired_[denseIndex] = 1U;
    }
    RefreshCompiledEffects(scene);
    ProcessOwnerLifecycle(scene);
    UpdateSharedSimulations();
    stepTelemetry_ = {};
    std::fill(renderRejectedByCapacity_.begin(), renderRejectedByCapacity_.end(), 0U);
    std::fill(renderRejectedBySpawnBudget_.begin(), renderRejectedBySpawnBudget_.end(), 0U);
    std::fill(renderRejectedByEventBudget_.begin(), renderRejectedByEventBudget_.end(), 0U);
    BeginEventStep();
    gpuScene_ = &scene;
    gpuSimTime_ = static_cast<double>(scene.Runtime().FixedStepIndex() + 1U) * static_cast<double>(fixedDeltaSeconds);
    ReleaseDueGpuEvents();
    std::uint32_t remainingSpawnBudget = kb::scene::kParticleEffectMaxSpawnsPerStep;
    for (std::uint32_t denseIndex = 0U; denseIndex < denseInstanceCount_; ++denseIndex) {
        if (sharedLeaders_[denseIndex] != kInvalidDenseIndex) {
            const InstanceRuntime& runtime = instanceRuntime_[denseIndex];
            if (std::any_of(runtime.liveParticles.begin(), runtime.liveParticles.end(), [](std::uint32_t n) { return n != 0U; })) {
                RemoveParticles(MakeInstanceId(denseToSlot_[denseIndex], slotGenerations_[denseToSlot_[denseIndex]]));
            }
        } else if (playbackStates_[denseIndex] == PlaybackState::Playing) {
            StepInstance(denseIndex, fixedDeltaSeconds, remainingSpawnBudget);
        }
    }
    AdvanceParticleAges(fixedDeltaSeconds);
    ExecuteForcesAndIntegrate(fixedDeltaSeconds);
    EmitWhileAlive(fixedDeltaSeconds, remainingSpawnBudget);
    static_cast<void>(ProcessInternalEvents(remainingSpawnBudget));
    FlushGpuSpawns();
    ProcessOwnerLifecycle(scene);
    return Result(kb::particles::ParticleRuntimeStatus::Success);
}

void CpuParticleBackend::FollowSimulationOrigin(const kb::math::DVec3& origin) noexcept {
    if (origin == simulationOrigin_) return;
    // The origin moves by whole grid steps, so the shift is exact for positions near it. GPU particles already
    // handed to the renderer keep the origin their commands carried.
    const kb::math::Vec3 shift = kb::math::ToVec3(simulationOrigin_ - origin);
    simulationOrigin_ = origin;
    for (kb::math::Vec3& position : particlePositions_) position = position + shift;
    for (kb::math::Vec3& position : particlePreviousPositions_) position = position + shift;
    for (std::uint32_t denseIndex = 0U; denseIndex < denseInstanceCount_; ++denseIndex) {
        ownerTransforms_[denseIndex].position = ownerTransforms_[denseIndex].position + shift;
    }
    for (std::vector<InternalEvent>* events : { &currentEvents_, &nextEvents_, &prewarmCurrentEvents_, &prewarmNextEvents_ }) {
        for (InternalEvent& event : *events) event.position = event.position + shift;
    }
    for (ScheduledGpuEvent& scheduled : gpuScheduledEvents_) scheduled.event.position = scheduled.event.position + shift;
}

float CpuParticleBackend::PlaneDistance(const kb::scene::ParticleCollisionPlaneModule& plane) const noexcept {
    return static_cast<float>(static_cast<double>(plane.distance) - (static_cast<double>(plane.normal.x) * simulationOrigin_.x +
        static_cast<double>(plane.normal.y) * simulationOrigin_.y + static_cast<double>(plane.normal.z) * simulationOrigin_.z));
}

kb::particles::ParticleRuntimeResult CpuParticleBackend::Simulate(
    kb::scene::Scene& scene,
    float fixedDeltaSeconds) {
    const auto stepped = Step(scene, fixedDeltaSeconds);
    if (!stepped.Succeeded()) return stepped;
    const auto published = PublishRenderSnapshot(
        scene, scene.Runtime().FixedStepIndex() + 1U);
    if (!published.Succeeded()) {
        return Result(kb::particles::ParticleRuntimeStatus::InvalidRequest);
    }
    return stepped;
}

kb::particles::ParticleRuntimeQueryResult CpuParticleBackend::Query(
    const kb::scene::Scene&,
    std::uint64_t instanceId) const noexcept {
    const std::uint32_t denseIndex = ResolveDenseIndex(instanceId);
    if (denseIndex == kInvalidDenseIndex) {
        return { .status = kb::particles::ParticleRuntimeStatus::InvalidInstance };
    }
    const CompiledEffect& effect =
        *compiledEffects_[instanceRuntime_[denseIndex].compiledEffectIndex].effect;
    return {
        .status = compiledEffects_[instanceRuntime_[denseIndex].compiledEffectIndex].invalidCandidateGeneration != 0U
            ? kb::particles::ParticleRuntimeStatus::StaleAfterInvalidReload
            : (reloadRestarted_[denseIndex] != 0U
                ? kb::particles::ParticleRuntimeStatus::Restarted
                : kb::particles::ParticleRuntimeStatus::Success),
        .state = playbackStates_[denseIndex] == PlaybackState::Playing,
        .assetId = effectAssetIds_[denseIndex],
        .materialAssetId = effect.emitterCount != 0U ? effect.emitters[0].materialAssetId : 0U,
        .owner = owners_[denseIndex],
        .liveParticleCount = LiveParticleCount(instanceId),
        .executionPath = executionPaths_[denseIndex],
        .gpuVisualAvailability = gpuVisualAvailability_[denseIndex],
        .requiresBackendRestart = gpuVisualRestartRequired_[denseIndex] != 0U,
    };
}

std::size_t CpuParticleBackend::CopyLiveInstanceIds(
    const kb::scene::Scene&,
    std::span<std::uint64_t> output) const noexcept {
    const std::size_t copied = std::min<std::size_t>(denseInstanceCount_, output.size());
    for (std::size_t index = 0U; index < copied; ++index) {
        output[index] = MakeInstanceId(denseToSlot_[index], slotGenerations_[denseToSlot_[index]]);
    }
    return denseInstanceCount_;
}

std::size_t CpuParticleBackend::CopyLiveParticleStates(
    const kb::scene::Scene&,
    std::uint64_t instanceId,
    std::span<kb::particles::ParticleRuntimeState> output) const noexcept {
    if (ResolveDenseIndex(instanceId) == kInvalidDenseIndex) return 0U;
    std::size_t total = 0U;
    for (std::size_t index = 0U; index < particleInstanceIds_.size(); ++index) {
        if (particleInstanceIds_[index] != instanceId) continue;
        if (total < output.size()) {
            output[total] = {
                .position = particlePositions_[index],
                .velocity = particleVelocities_[index],
                .age = particleAges_[index],
                .lifetime = particleLifetimes_[index],
                .color = particleColors_[index],
                .size = particleSizes_[index],
            };
        }
        ++total;
    }
    return total;
}

void CpuParticleBackend::RemoveDenseInstance(std::uint32_t denseIndex) noexcept {
    const std::uint32_t removedSlot = denseToSlot_[denseIndex];
    const std::uint32_t last = --denseInstanceCount_;
    if (denseIndex != last) {
        denseToSlot_[denseIndex] = denseToSlot_[last];
        slotToDense_[denseToSlot_[denseIndex]] = denseIndex;
        effectAssetIds_[denseIndex] = effectAssetIds_[last];
        owners_[denseIndex] = owners_[last];
        ownerDeathPolicies_[denseIndex] = ownerDeathPolicies_[last];
        ownerTerminalPending_[denseIndex] = ownerTerminalPending_[last];
        rateMultipliers_[denseIndex] = rateMultipliers_[last];
        maxParticlesOverrides_[denseIndex] = maxParticlesOverrides_[last];
        followTransforms_[denseIndex] = followTransforms_[last];
        ownerTransforms_[denseIndex] = ownerTransforms_[last];
        reloadRestarted_[denseIndex] = reloadRestarted_[last];
        executionPaths_[denseIndex] = executionPaths_[last];
        gpuVisualAvailability_[denseIndex] = gpuVisualAvailability_[last];
        gpuVisualCapabilityEpochs_[denseIndex] = gpuVisualCapabilityEpochs_[last];
        gpuVisualRestartRequired_[denseIndex] = gpuVisualRestartRequired_[last];
        seeds_[denseIndex] = seeds_[last];
        playbackStates_[denseIndex] = playbackStates_[last];
        instanceRuntime_[denseIndex] = instanceRuntime_[last];
        for (std::uint8_t emitterIndex = 0U; emitterIndex < kb::scene::kParticleEffectMaxEmitters; ++emitterIndex) {
            const std::size_t destinationGroup = RenderGroupIndex(denseIndex, emitterIndex);
            const std::size_t sourceGroup = RenderGroupIndex(last, emitterIndex);
            renderRejectedByCapacity_[destinationGroup] = renderRejectedByCapacity_[sourceGroup];
            renderRejectedBySpawnBudget_[destinationGroup] = renderRejectedBySpawnBudget_[sourceGroup];
            renderRejectedByEventBudget_[destinationGroup] = renderRejectedByEventBudget_[sourceGroup];
        }
    }
    for (std::uint8_t emitterIndex = 0U; emitterIndex < kb::scene::kParticleEffectMaxEmitters; ++emitterIndex) {
        const std::size_t releasedGroup = RenderGroupIndex(last, emitterIndex);
        renderRejectedByCapacity_[releasedGroup] = 0U;
        renderRejectedBySpawnBudget_[releasedGroup] = 0U;
        renderRejectedByEventBudget_[releasedGroup] = 0U;
    }
    slotToDense_[removedSlot] = kInvalidDenseIndex;
    std::uint32_t& generation = slotGenerations_[removedSlot];
    ++generation;
    if (generation == 0U) generation = 1U;
}

void CpuParticleBackend::RemoveParticles(std::uint64_t instanceId) noexcept {
    std::size_t index = 0U;
    while (index < particleInstanceIds_.size()) {
        if (particleInstanceIds_[index] != instanceId) {
            ++index;
            continue;
        }
        const std::size_t last = particleInstanceIds_.size() - 1U;
        particleInstanceIds_[index] = particleInstanceIds_[last];
        particleIds_[index] = particleIds_[last];
        particleSpawnOrdinals_[index] = particleSpawnOrdinals_[last];
        particleRibbonGroups_[index] = particleRibbonGroups_[last];
        particleEmitterIndices_[index] = particleEmitterIndices_[last];
        particlePositions_[index] = particlePositions_[last];
        particlePreviousPositions_[index] = particlePreviousPositions_[last];
        particleVelocities_[index] = particleVelocities_[last];
        particleAges_[index] = particleAges_[last];
        particleLifetimes_[index] = particleLifetimes_[last];
        particleColors_[index] = particleColors_[last];
        particleSizes_[index] = particleSizes_[last];
        particleSpins_[index] = particleSpins_[last];
        particleSpinRates_[index] = particleSpinRates_[last];
        particleSizeScales_[index] = particleSizeScales_[last];
        particleEventDepths_[index] = particleEventDepths_[last];
        particlePrewarmGroups_[index] = particlePrewarmGroups_[last];
        particleInstanceIds_.pop_back();
        particleIds_.pop_back();
        particleSpawnOrdinals_.pop_back();
        particleRibbonGroups_.pop_back();
        particleEmitterIndices_.pop_back();
        particlePositions_.pop_back();
        particlePreviousPositions_.pop_back();
        particleVelocities_.pop_back();
        particleAges_.pop_back();
        particleLifetimes_.pop_back();
        particleColors_.pop_back();
        particleSizes_.pop_back();
        particleSpins_.pop_back();
        particleSpinRates_.pop_back();
        particleSizeScales_.pop_back();
        particleEventDepths_.pop_back();
        particlePrewarmGroups_.pop_back();
    }
    const std::uint32_t denseIndex = ResolveDenseIndex(instanceId);
    if (denseIndex != kInvalidDenseIndex) instanceRuntime_[denseIndex].liveParticles.fill(0U);
}

std::uint32_t CpuParticleBackend::AcquireCompiledEffect(
    kb::scene::Scene& scene,
    std::uint64_t effectAssetId,
    kb::particles::ParticleRuntimeStatus& failureStatus) {
    kb::assets::AssetManager& manager = scene.Assets().Manager();
    const kb::assets::AssetId assetId{effectAssetId};
    const std::uint64_t generation = manager.LoadGeneration(assetId);
    for (std::uint32_t index = 0U; index < compiledEffects_.size(); ++index) {
        CompiledEffectEntry& cached = compiledEffects_[index];
        if (cached.referenceCount != 0U && cached.assetId == effectAssetId &&
            cached.assetGeneration == generation) {
            ++cached.referenceCount;
            return index;
        }
    }

    const auto asset = manager.Load<kb::scene::ParticleEffectAsset>(assetId);
    const kb::assets::AssetMetadata* metadata = manager.Registry().Find(assetId);
    if (!asset.IsLoaded() || metadata == nullptr) {
        lastEffectFailure_ = manager.LastError();
        return kInvalidDenseIndex;
    }

    const ParticleCompileResult compiled = ParticleEffectCompiler::Compile(*asset, *metadata, manager.Registry());
    if (!compiled.Succeeded()) {
        lastEffectFailure_.clear();
        for (const kb::scene::ParticleEffectDiagnostic& diagnostic : compiled.diagnostics) {
            if (!lastEffectFailure_.empty()) lastEffectFailure_ += "; ";
            lastEffectFailure_ += diagnostic.propertyPath + ": " + diagnostic.message;
        }
        failureStatus = std::any_of(compiled.diagnostics.begin(), compiled.diagnostics.end(), [](const auto& diagnostic) {
            return diagnostic.code == kb::scene::ParticleEffectDiagnosticCode::UnsupportedCapability;
        }) ? kb::particles::ParticleRuntimeStatus::UnsupportedOutput
           : kb::particles::ParticleRuntimeStatus::InvalidAsset;
        return kInvalidDenseIndex;
    }

    // Mesh spawn shapes: the renderer's mesh asset (level 0) through the scene's asset manager.
    for (std::uint8_t e = 0U; e < compiled.effect->emitterCount; ++e) {
        const std::uint64_t meshId = compiled.effect->emitters[e].shapeMeshAssetId;
        if (compiled.effect->emitters[e].shape.type != kb::scene::ParticleSpawnShape::Mesh || meshId == 0U ||
            meshSurfaces_.contains(meshId)) continue;
        [[maybe_unused]] MeshSurface& surface = meshSurfaces_[meshId];
#if KB_21KB_PARTICLE_MESH_SHAPE
        const auto mesh = manager.Load<kb::render::RenderMeshAssetData>(kb::assets::AssetId{meshId});
        if (!mesh.IsLoaded()) continue;
        const std::size_t vertexCount = mesh->vertices.empty() ? mesh->tangentVertices.size() : mesh->vertices.size();
        const std::size_t indexCount = mesh->indices32.empty() ? mesh->indices16.size() : mesh->indices32.size();
        const auto corner = [&](std::size_t index, std::uint32_t vertexStart, kb::math::Vec3& out) {
            const std::size_t v = std::size_t{vertexStart} + (mesh->indices32.empty() ? mesh->indices16[index] : mesh->indices32[index]);
            if (v >= vertexCount) return false;
            out = mesh->vertices.empty() ? kb::math::Vec3{mesh->tangentVertices[v].x, mesh->tangentVertices[v].y, mesh->tangentVertices[v].z}
                                         : kb::math::Vec3{mesh->vertices[v].x, mesh->vertices[v].y, mesh->vertices[v].z};
            return true;
        };
        const auto addRange = [&](std::size_t start, std::size_t count, std::uint32_t vertexStart) {
            for (std::size_t i = start; i + 3U <= start + count && i + 3U <= indexCount; i += 3U) {
                kb::math::Vec3 t[3];
                if (!corner(i, vertexStart, t[0]) || !corner(i + 1U, vertexStart, t[1]) || !corner(i + 2U, vertexStart, t[2])) continue;
                const kb::math::Vec3 c = kb::math::Cross(t[1] - t[0], t[2] - t[0]);
                surface.area.push_back((surface.area.empty() ? 0.0F : surface.area.back()) + 0.5F * std::sqrt(kb::math::Dot(c, c)));
                surface.corners.insert(surface.corners.end(), t, t + 3);
            }
        };
        if (mesh->sections.empty()) addRange(0U, indexCount, 0U);
        for (const kb::render::RenderMeshSectionDesc& section : mesh->sections) {
            if (section.lodLevel == 0U && section.terrainLayerIndex == UINT8_MAX)
                addRange(section.indexStart, section.indexCount, section.vertexStart);
        }
#endif
    }

    std::uint32_t freeIndex = kInvalidDenseIndex;
    for (std::uint32_t index = 0U; index < compiledEffects_.size(); ++index) {
        if (compiledEffects_[index].referenceCount == 0U) {
            freeIndex = index;
            break;
        }
    }
    if (freeIndex == kInvalidDenseIndex) return kInvalidDenseIndex;

    compiledEffects_[freeIndex] = {
        .assetId = effectAssetId,
        .assetGeneration = generation,
        .referenceCount = 1U,
        .effect = compiled.effect,
    };
    return freeIndex;
}

void CpuParticleBackend::ReleaseCompiledEffect(std::uint32_t index) noexcept {
    if (index < compiledEffects_.size() && compiledEffects_[index].referenceCount != 0U) {
        --compiledEffects_[index].referenceCount;
    }
}

bool CpuParticleBackend::TopologyCompatible(
    const CompiledEffect& previous,
    const CompiledEffect& candidate) noexcept {
    if (previous.emitterCount != candidate.emitterCount ||
        previous.eventBindingCount != candidate.eventBindingCount) {
        return false;
    }
    for (std::uint8_t emitterIndex = 0U; emitterIndex < previous.emitterCount; ++emitterIndex) {
        const CompiledEmitter& lhs = previous.emitters[emitterIndex];
        const CompiledEmitter& rhs = candidate.emitters[emitterIndex];
        if (lhs.emitterId != rhs.emitterId || lhs.outputType != rhs.outputType ||
            lhs.simulationSpace != rhs.simulationSpace || lhs.maxParticles != rhs.maxParticles ||
            lhs.moduleCount != rhs.moduleCount) {
            return false;
        }
        for (std::uint8_t moduleIndex = 0U; moduleIndex < lhs.moduleCount; ++moduleIndex) {
            const CompiledEmitter::Module& lhsModule = lhs.modules[moduleIndex];
            const CompiledEmitter::Module& rhsModule = rhs.modules[moduleIndex];
            if (lhsModule.moduleId != rhsModule.moduleId || lhsModule.type != rhsModule.type) return false;
            if (lhsModule.type == kb::scene::ParticleModuleType::SubEmitter) {
                const auto& lhsSub = std::get<kb::scene::ParticleSubEmitterModule>(lhsModule.payload);
                const auto& rhsSub = std::get<kb::scene::ParticleSubEmitterModule>(rhsModule.payload);
                if (lhsSub.targetEmitterId != rhsSub.targetEmitterId || lhsSub.trigger != rhsSub.trigger) return false;
            }
        }
    }
    for (std::uint8_t bindingIndex = 0U; bindingIndex < previous.eventBindingCount; ++bindingIndex) {
        const CompiledEffect::EventBinding& lhs = previous.eventBindings[bindingIndex];
        const CompiledEffect::EventBinding& rhs = candidate.eventBindings[bindingIndex];
        if (lhs.sourceEmitterIndex != rhs.sourceEmitterIndex || lhs.trigger != rhs.trigger ||
            lhs.sourceModuleId != rhs.sourceModuleId || lhs.targetEmitterIndex != rhs.targetEmitterIndex) {
            return false;
        }
    }
    return true;
}

void CpuParticleBackend::RefreshCompiledEffects(kb::scene::Scene& scene) {
    kb::assets::AssetManager& manager = scene.Assets().Manager();
    for (std::uint32_t previousIndex = 0U; previousIndex < compiledEffects_.size(); ++previousIndex) {
        CompiledEffectEntry& previous = compiledEffects_[previousIndex];
        if (previous.referenceCount == 0U) continue;
        const std::uint64_t generation = manager.LoadGeneration(kb::assets::AssetId{previous.assetId});
        if (previous.assetGeneration == generation || previous.invalidCandidateGeneration == generation) continue;

        kb::particles::ParticleRuntimeStatus failure = kb::particles::ParticleRuntimeStatus::InvalidAsset;
        const std::uint32_t candidateIndex = AcquireCompiledEffect(scene, previous.assetId, failure);
        if (candidateIndex == kInvalidDenseIndex) {
            previous.invalidCandidateGeneration = generation;
            continue;
        }
        CompiledEffectEntry& candidate = compiledEffects_[candidateIndex];
        --candidate.referenceCount;
        const bool compatible = TopologyCompatible(*previous.effect, *candidate.effect);
        const std::uint16_t transferredReferences = previous.referenceCount;
        previous.referenceCount = 0U;
        candidate.referenceCount = static_cast<std::uint16_t>(candidate.referenceCount + transferredReferences);
        candidate.invalidCandidateGeneration = 0U;

        for (std::uint32_t denseIndex = 0U; denseIndex < denseInstanceCount_; ++denseIndex) {
            if (instanceRuntime_[denseIndex].compiledEffectIndex != previousIndex) continue;
            instanceRuntime_[denseIndex].compiledEffectIndex = candidateIndex;
            if (compatible) continue;
            const std::uint64_t instanceId =
                MakeInstanceId(denseToSlot_[denseIndex], slotGenerations_[denseToSlot_[denseIndex]]);
            const PlaybackState previousState = playbackStates_[denseIndex];
            RemoveParticles(instanceId);
            RemoveQueuedEvents(instanceId);
            ResetInstance(denseIndex);
            reloadRestarted_[denseIndex] = 1U;
            playbackStates_[denseIndex] = previousState == PlaybackState::Draining
                ? PlaybackState::Stopped : previousState;
            if (playbackStates_[denseIndex] == PlaybackState::Playing &&
                PrewarmInstance(denseIndex) != kb::particles::ParticleRuntimeStatus::Success) {
                RemoveParticles(instanceId);
                ResetInstance(denseIndex);
                playbackStates_[denseIndex] = PlaybackState::Stopped;
            }
        }
    }
}

bool CpuParticleBackend::FinishOwnerLifecycle(kb::scene::Scene& scene, std::uint32_t denseIndex) noexcept {
    const std::uint64_t instanceId =
        MakeInstanceId(denseToSlot_[denseIndex], slotGenerations_[denseToSlot_[denseIndex]]);
    const kb::particles::ParticleRuntimeResult queued = kb::particles::ParticlePlayback::QueueEvent(scene, {
        .target = owners_[denseIndex],
        .instanceId = instanceId,
        .effectAssetId = effectAssetIds_[denseIndex],
    });
    if (!queued.Succeeded()) return false;
    ReleaseCompiledEffect(instanceRuntime_[denseIndex].compiledEffectIndex);
    RemoveDenseInstance(denseIndex);
    return true;
}

void CpuParticleBackend::ProcessOwnerLifecycle(kb::scene::Scene& scene) noexcept {
    std::uint32_t denseIndex = 0U;
    while (denseIndex < denseInstanceCount_) {
        if (scene.Entities().IsAlive(owners_[denseIndex])) {
            ++denseIndex;
            continue;
        }
        const std::uint64_t instanceId =
            MakeInstanceId(denseToSlot_[denseIndex], slotGenerations_[denseToSlot_[denseIndex]]);
        if (ownerTerminalPending_[denseIndex] == 0U) {
            ownerTerminalPending_[denseIndex] = 1U;
            playbackStates_[denseIndex] = PlaybackState::Draining;
            if (ownerDeathPolicies_[denseIndex] == kb::scene::ParticleOwnerDeathPolicy::Clear) {
                RemoveParticles(instanceId);
            }
        }
        if (LiveParticleCount(instanceId) == 0U && FinishOwnerLifecycle(scene, denseIndex)) continue;
        ++denseIndex;
    }
}

void CpuParticleBackend::ResetInstance(std::uint32_t denseIndex) noexcept {
    InstanceRuntime& runtime = instanceRuntime_[denseIndex];
    const std::uint32_t compiledIndex = runtime.compiledEffectIndex;
    runtime = {};
    runtime.compiledEffectIndex = compiledIndex;
    const std::uint64_t instanceId = MakeInstanceId(denseToSlot_[denseIndex], slotGenerations_[denseToSlot_[denseIndex]]);
    RemoveQueuedEvents(instanceId);
    runtime.randomState = seeds_[denseIndex] ^ (instanceId * 0x9E3779B97F4A7C15ULL);
}

kb::particles::ParticleRuntimeStatus CpuParticleBackend::PrewarmInstance(std::uint32_t denseIndex) noexcept {
    const CompiledEffect& effect = *compiledEffects_[instanceRuntime_[denseIndex].compiledEffectIndex].effect;
    const std::uint64_t instanceId =
        MakeInstanceId(denseToSlot_[denseIndex], slotGenerations_[denseToSlot_[denseIndex]]);
    prewarmingInstanceId_ = instanceId;
    prewarmCurrentEvents_.clear();
    prewarmNextEvents_.clear();
    for (std::uint8_t emitterIndex = 0U; emitterIndex < effect.emitterCount; ++emitterIndex) {
        const std::uint32_t steps = static_cast<std::uint32_t>(std::floor(
            static_cast<double>(effect.emitters[emitterIndex].prewarmSeconds) /
            static_cast<double>(kb::scene::kSceneRuntimeDefaultFixedDeltaSeconds) + 0.5));
        for (std::uint32_t step = 0U; step < steps; ++step) {
            stepTelemetry_ = {};
            BeginEventStep();
            std::uint32_t budget = kb::scene::kParticleEffectMaxSpawnsPerStep;
            StepEmitter(denseIndex, emitterIndex, kb::scene::kSceneRuntimeDefaultFixedDeltaSeconds, budget,
                emitterIndex);
            AdvanceParticleAges(kb::scene::kSceneRuntimeDefaultFixedDeltaSeconds, instanceId, emitterIndex);
            ExecuteForcesAndIntegrate(
                kb::scene::kSceneRuntimeDefaultFixedDeltaSeconds, instanceId, emitterIndex);
            const kb::particles::ParticleRuntimeStatus status = ProcessInternalEvents(budget);
            if (status != kb::particles::ParticleRuntimeStatus::Success) {
                prewarmCurrentEvents_.clear();
                prewarmNextEvents_.clear();
                prewarmingInstanceId_ = 0U;
                return status;
            }
        }
    }
    if (prewarmCurrentEvents_.size() + prewarmNextEvents_.size() >
        kb::scene::kParticleEffectMaxEventsPerStep - nextEvents_.size()) {
        prewarmCurrentEvents_.clear();
        prewarmNextEvents_.clear();
        prewarmingInstanceId_ = 0U;
        return kb::particles::ParticleRuntimeStatus::EventQueueFull;
    }
    nextEvents_.insert(nextEvents_.end(), prewarmCurrentEvents_.begin(), prewarmCurrentEvents_.end());
    nextEvents_.insert(nextEvents_.end(), prewarmNextEvents_.begin(), prewarmNextEvents_.end());
    prewarmCurrentEvents_.clear();
    prewarmNextEvents_.clear();
    prewarmingInstanceId_ = 0U;
    return kb::particles::ParticleRuntimeStatus::Success;
}

float CpuParticleBackend::EvaluateRate(const CompiledEmitter& emitter, float timeSeconds) const noexcept {
    if (emitter.rateKeyCount == 0U) return 0.0F;
    if (emitter.rateKeyCount == 1U || timeSeconds <= emitter.rateKeys[0].time) return emitter.rateKeys[0].value;
    const CompiledCurveKey& last = emitter.rateKeys[emitter.rateKeyCount - 1U];
    if (timeSeconds >= last.time) return last.value;
    for (std::uint8_t index = 0U; index + 1U < emitter.rateKeyCount; ++index) {
        const CompiledCurveKey& from = emitter.rateKeys[index];
        const CompiledCurveKey& to = emitter.rateKeys[index + 1U];
        if (timeSeconds <= to.time) {
            const float alpha = (timeSeconds - from.time) / (to.time - from.time);
            const float eased = kb::math::Evaluate(from.easing, alpha);
            return from.value + (to.value - from.value) * eased;
        }
    }
    return last.value;
}

float CpuParticleBackend::EvaluateCurve(const CompiledCurve& curve, float normalizedAge) noexcept {
    if (curve.keyCount == 1U || normalizedAge <= curve.keys[0].time) return curve.keys[0].value;
    const CompiledCurveKey& last = curve.keys[curve.keyCount - 1U];
    if (normalizedAge >= last.time) return last.value;
    for (std::uint8_t index = 0U; index + 1U < curve.keyCount; ++index) {
        const CompiledCurveKey& from = curve.keys[index];
        const CompiledCurveKey& to = curve.keys[index + 1U];
        if (normalizedAge <= to.time) {
            const float alpha = (normalizedAge - from.time) / (to.time - from.time);
            return kb::math::Lerp(from.value, to.value, kb::math::Evaluate(from.easing, alpha));
        }
    }
    return last.value;
}

kb::math::Color CpuParticleBackend::EvaluateGradient(
    const CompiledGradient& gradient,
    float normalizedAge) noexcept {
    if (gradient.stopCount == 0U) return {};
    if (gradient.stopCount == 1U || normalizedAge <= gradient.stops[0].time) {
        return gradient.stops[0].color;
    }
    const CompiledGradientStop& last = gradient.stops[gradient.stopCount - 1U];
    if (normalizedAge >= last.time) return last.color;
    for (std::uint8_t index = 0U; index + 1U < gradient.stopCount; ++index) {
        const CompiledGradientStop& from = gradient.stops[index];
        const CompiledGradientStop& to = gradient.stops[index + 1U];
        if (normalizedAge <= to.time) {
            const float alpha = (normalizedAge - from.time) / (to.time - from.time);
            return {
                kb::math::Lerp(from.color.r, to.color.r, alpha),
                kb::math::Lerp(from.color.g, to.color.g, alpha),
                kb::math::Lerp(from.color.b, to.color.b, alpha),
                kb::math::Lerp(from.color.a, to.color.a, alpha),
            };
        }
    }
    return last.color;
}

float CpuParticleBackend::NextRandom01(InstanceRuntime& runtime) noexcept {
    runtime.randomState += 0x9E3779B97F4A7C15ULL;
    std::uint64_t value = runtime.randomState;
    value = (value ^ (value >> 30U)) * 0xBF58476D1CE4E5B9ULL;
    value = (value ^ (value >> 27U)) * 0x94D049BB133111EBULL;
    value ^= value >> 31U;
    return static_cast<float>(value >> 40U) * (1.0F / 16'777'216.0F);
}

kb::math::Vec3 CpuParticleBackend::SampleInitialVelocity(
    const CompiledEmitter& emitter,
    InstanceRuntime& runtime) noexcept {
    const kb::scene::ParticleInitialVelocityModule& initial = emitter.initialVelocity;
    const float speedSample = NextRandom01(runtime);
    const float coneSample = NextRandom01(runtime);
    const float azimuthSample = NextRandom01(runtime);
    const float randomizedSpeedSample = 0.5F + (speedSample - 0.5F) * initial.randomization;
    const float speed = initial.speedMin + (initial.speedMax - initial.speedMin) * randomizedSpeedSample;
    const float coneRadians = kb::math::ToRadians(
        kb::math::Degrees{initial.spreadDegrees * initial.randomization}).Value();
    const float minimumCosine = std::cos(coneRadians);
    const float cosine = 1.0F - coneSample * (1.0F - minimumCosine);
    const float sine = std::sqrt(std::max(0.0F, 1.0F - cosine * cosine));
    const float azimuth = 2.0F * kb::math::kPi * azimuthSample;
    const kb::math::Vec3 axis = initial.direction;
    const kb::math::Vec3 reference = std::abs(axis.z) < 0.999F
        ? kb::math::Vec3{0.0F, 0.0F, 1.0F}
        : kb::math::Vec3{0.0F, 1.0F, 0.0F};
    const kb::math::Vec3 tangent = kb::math::Normalize(kb::math::Cross(reference, axis));
    const kb::math::Vec3 bitangent = kb::math::Cross(axis, tangent);
    const kb::math::Vec3 direction = axis * cosine +
        tangent * (sine * std::cos(azimuth)) + bitangent * (sine * std::sin(azimuth));
    return direction * speed;
}

kb::math::Vec3 CpuParticleBackend::SampleShape(const CompiledEmitter& emitter, InstanceRuntime& runtime,
                                               kb::math::Vec3& normal) noexcept {
    const float twoPi = 2.0F * kb::math::kPi;
    const kb::scene::ParticleSpawnShapeAsset& shape = emitter.shape;
    kb::math::Vec3 point{};
    normal = {0.0F, 1.0F, 0.0F};
    switch (shape.type) {
    case kb::scene::ParticleSpawnShape::Point:
        break;
    case kb::scene::ParticleSpawnShape::Sphere: {
        const float u = NextRandom01(runtime) * 2.0F - 1.0F, a = NextRandom01(runtime) * twoPi;
        const float s = std::sqrt(std::max(0.0F, 1.0F - u * u));
        normal = {s * std::cos(a), shape.upperHalfOnly ? std::abs(u) : u, s * std::sin(a)};
        point = normal * (shape.surfaceOnly ? shape.radius : shape.radius * std::cbrt(NextRandom01(runtime)));
        break;
    }
    case kb::scene::ParticleSpawnShape::Box:
        point = {(NextRandom01(runtime) - 0.5F) * shape.boxSize.x, (NextRandom01(runtime) - 0.5F) * shape.boxSize.y,
                 (NextRandom01(runtime) - 0.5F) * shape.boxSize.z};
        if (kb::math::Dot(point, point) > 1.0e-10F) normal = kb::math::Normalize(point);
        break;
    case kb::scene::ParticleSpawnShape::Cone: {
        // Base disc; the normal leans outward with the distance from the axis, up to the angle at the rim.
        const float a = NextRandom01(runtime) * twoPi, rr = std::sqrt(NextRandom01(runtime)) * shape.radius;
        const float angle = shape.coneAngleDegrees * 0.017453292F;
        point = {std::cos(a) * rr, 0.0F, std::sin(a) * rr};
        const float lean = shape.radius > 1.0e-4F ? rr / shape.radius : 0.0F;
        normal = kb::math::Normalize(kb::math::Vec3{std::cos(a) * std::sin(angle) * lean, std::cos(angle),
                                                    std::sin(a) * std::sin(angle) * lean});
        break;
    }
    case kb::scene::ParticleSpawnShape::Circle: {
        const float a = NextRandom01(runtime) * twoPi;
        normal = {std::cos(a), 0.0F, std::sin(a)};
        point = normal * shape.radius;
        break;
    }
    case kb::scene::ParticleSpawnShape::Disc: {
        const float a = NextRandom01(runtime) * twoPi;
        normal = {std::cos(a), 0.0F, std::sin(a)};
        point = normal * (std::sqrt(NextRandom01(runtime)) * shape.radius);
        break;
    }
    case kb::scene::ParticleSpawnShape::Line:
        point = shape.lineStart + (shape.lineEnd - shape.lineStart) * NextRandom01(runtime);
        break;
    case kb::scene::ParticleSpawnShape::Spiral: {
        const float arms = std::max(1.0F, shape.spiralArms);
        const float d = std::pow(NextRandom01(runtime), 0.7F) * shape.radius;
        const float arm = std::floor(NextRandom01(runtime) * arms);
        const float a = arm * twoPi / arms + d * shape.spiralTwist + (NextRandom01(runtime) - 0.5F) * 0.6F;
        point = {std::cos(a) * d, (NextRandom01(runtime) - 0.5F) * shape.spiralThickness, std::sin(a) * d};
        if (kb::math::Dot(point, point) > 1.0e-10F) normal = kb::math::Normalize(point);
        break;
    }
    case kb::scene::ParticleSpawnShape::Mesh: {
        // A triangle by its area, a uniform point on it; normal = the face's.
        const auto found = meshSurfaces_.find(emitter.shapeMeshAssetId);
        if (found == meshSurfaces_.end() || found->second.area.empty()) break;
        const MeshSurface& s = found->second;
        const auto hit = std::upper_bound(s.area.begin(), s.area.end(), NextRandom01(runtime) * s.area.back());
        const std::size_t t = std::min<std::size_t>(static_cast<std::size_t>(hit - s.area.begin()), s.area.size() - 1U) * 3U;
        float a = NextRandom01(runtime), b = NextRandom01(runtime);
        if (a + b > 1.0F) { a = 1.0F - a; b = 1.0F - b; }
        const kb::math::Vec3 e1 = s.corners[t + 1U] - s.corners[t], e2 = s.corners[t + 2U] - s.corners[t];
        point = s.corners[t] + e1 * a + e2 * b;
        const kb::math::Vec3 c = kb::math::Cross(e1, e2);
        if (kb::math::Dot(c, c) > 1.0e-20F) normal = kb::math::Normalize(c);
        break;
    }
    }
    return point;
}

kb::math::Vec3 CpuParticleBackend::SampleStartVelocity(const CompiledEmitter& emitter, InstanceRuntime& runtime,
                                                       std::uint8_t emitterIndex, kb::math::Vec3 normal) noexcept {
    switch (emitter.velocityMode) {
    case kb::scene::ParticleVelocityMode::Radial: {
        const kb::scene::ParticleInitialVelocityModule& v = emitter.initialVelocity;
        return normal * (v.speedMin + (v.speedMax - v.speedMin) * NextRandom01(runtime));
    }
    case kb::scene::ParticleVelocityMode::Spiral2D: {
        // One of the arms in the XY plane (the screen of a 2D game), the arms turning with the emitter's time.
        const float a = std::floor(NextRandom01(runtime) * emitter.spiralArms) * 2.0F * kb::math::kPi / emitter.spiralArms +
            runtime.elapsedSeconds[emitterIndex] * emitter.spiralTurnRadians;
        return kb::math::Vec3{std::cos(a), std::sin(a), 0.0F} * emitter.initialVelocity.speedMin;
    }
    case kb::scene::ParticleVelocityMode::Cone:
        break;
    }
    return SampleInitialVelocity(emitter, runtime);
}

kb::math::Vec3 CpuParticleBackend::EmitterOrigin(std::uint32_t denseIndex, const CompiledEmitter& emitter) const noexcept {
    if (emitter.absolutePosition) return emitter.localPosition - kb::math::ToVec3(simulationOrigin_);
    const kb::scene::WorldTransform& owner = ownerTransforms_[denseIndex];
    return owner.position + TransformDirection(owner, Scale(emitter.localPosition, owner.scale));
}

bool CpuParticleBackend::SpawnExact(
    std::uint32_t denseIndex,
    std::uint8_t emitterIndex,
    std::uint32_t count,
    std::uint8_t eventDepth,
    std::uint8_t prewarmGroup,
    const kb::math::Vec3* eventPosition) noexcept {
    InstanceRuntime& runtime = instanceRuntime_[denseIndex];
    const CompiledEffect& effect = *compiledEffects_[runtime.compiledEffectIndex].effect;
    if (emitterIndex >= effect.emitterCount) return false;
    if (GpuEligible(denseIndex, emitterIndex, prewarmGroup)) {
        // A sub-emitter child or a manual emit of a GPU emitter is a birth record like any other.
        SpawnGpu(denseIndex, emitterIndex, count, eventPosition, eventDepth);
        return true;
    }
    const CompiledEmitter& emitter = effect.emitters[emitterIndex];
    const std::uint32_t instanceLive = LiveParticleCount(
        MakeInstanceId(denseToSlot_[denseIndex], slotGenerations_[denseToSlot_[denseIndex]]));
    const std::uint32_t instanceLimit = InstanceParticleLimit(denseIndex);
    // A beam birth is all its points.
    const std::uint32_t points = count * (emitter.beam.enabled ? emitter.beam.segments + 1U : 1U);
    if (points > emitter.maxParticles - runtime.liveParticles[emitterIndex] ||
        instanceLive >= instanceLimit || points > instanceLimit - instanceLive ||
        points > kb::scene::kParticleEffectMaxCpuParticlesPerScene - particleInstanceIds_.size()) {
        return false;
    }
    const std::uint64_t instanceId = MakeInstanceId(denseToSlot_[denseIndex], slotGenerations_[denseToSlot_[denseIndex]]);
    const InternalEvent birthPrototype{
        .instanceId = instanceId,
        .sourceEmitterIndex = emitterIndex,
        .trigger = kb::scene::ParticleEventTrigger::Birth,
        .depth = eventDepth,
        .prewarmGroup = prewarmGroup,
    };
    const std::size_t queuedBirthEvents = prewarmingInstanceId_ == instanceId
        ? prewarmNextEvents_.size() : nextEvents_.size();
    if (EventHasAction(birthPrototype) &&
        count > kb::scene::kParticleEffectMaxEventsPerStep - queuedBirthEvents) {
        stepTelemetry_.rejectedByEventBudget += count - static_cast<std::uint32_t>(
            kb::scene::kParticleEffectMaxEventsPerStep - queuedBirthEvents);
        renderRejectedByEventBudget_[RenderGroupIndex(denseIndex, emitterIndex)] +=
            count - static_cast<std::uint32_t>(
                kb::scene::kParticleEffectMaxEventsPerStep - queuedBirthEvents);
        eventQueueOverflowed_ = true;
        return false;
    }
    const kb::scene::WorldTransform& ownerTransform = ownerTransforms_[denseIndex];
    const kb::math::Vec3 emitterOffset = TransformDirection(ownerTransform,
        Scale(emitter.localPosition, ownerTransform.scale));
    // The emitter's frame: the owner's rotation, then the emitter's own (shapes and start velocities).
    const kb::math::Quat frame = ownerTransform.rotation * emitter.localRotation;
    const kb::math::Vec3 birthOrigin = emitter.absolutePosition ? EmitterOrigin(denseIndex, emitter)
        : eventPosition != nullptr ? *eventPosition + emitterOffset : ownerTransform.position + emitterOffset;
    // Shape point and start velocity of a birth, emitter space (SampleShape, SampleStartVelocity).
    const auto sampleShape = [&](kb::math::Vec3& point, kb::math::Vec3& normal) noexcept {
        point = SampleShape(emitter, runtime, normal);
    };
    const auto sampleVelocity = [&](kb::math::Vec3 normal) noexcept -> kb::math::Vec3 {
        return SampleStartVelocity(emitter, runtime, emitterIndex, normal);
    };
    const std::uint32_t perBirth = emitter.beam.enabled ? emitter.beam.segments + 1U : 1U;
    for (std::uint32_t index = 0U; index < count * perBirth; ++index) {
        const std::uint32_t pointOfBeam = index % perBirth;
        // A beam lives and dies whole (Unity, Niagara): its points share one lifetime, so the strip
        // never loses points mid-life and its texture does not slide along it.
        if (emitter.beam.enabled && pointOfBeam == 0U) {
            runtime.beamLifetime = emitter.lifetimeMin +
                (emitter.lifetimeMax - emitter.lifetimeMin) * NextRandom01(runtime);
        }
        const float lifetime = emitter.beam.enabled ? runtime.beamLifetime
            : emitter.lifetimeMin + (emitter.lifetimeMax - emitter.lifetimeMin) * NextRandom01(runtime);
        particleInstanceIds_.push_back(instanceId);
        particleIds_.push_back(nextParticleId_++);
        if (nextParticleId_ == 0U) nextParticleId_ = 1U;
        particleSpawnOrdinals_.push_back(runtime.spawnOrdinals[emitterIndex]);
        // A beam's points share its ribbon group (one beam, one ribbon).
        particleRibbonGroups_.push_back(emitter.beam.enabled
            ? static_cast<std::uint32_t>(runtime.spawnOrdinals[emitterIndex] / perBirth) : 0U);
        particleEmitterIndices_.push_back(emitterIndex);
        if (emitter.beam.enabled) {
            // Beam point: from the beam's start to its end, pushed sideways (most at the middle).
            if (pointOfBeam == 0U) {
                kb::math::Vec3 from = emitter.beam.start, to = emitter.beam.end;
                if (emitter.beam.ends != kb::scene::ParticleBeamEnds::Fixed) {
                    kb::math::Vec3 point, normal;
                    sampleShape(point, normal);
                    point = kb::math::Rotate(frame, point);
                    if (emitter.beam.ends == kb::scene::ParticleBeamEnds::ShapeToEmitter) {
                        from = point;
                        to = {};
                    } else {
                        from = from + kb::math::Vec3{point.x, 0.0F, point.z};
                        to = to + kb::math::Vec3{point.x, 0.0F, point.z};
                    }
                }
                runtime.beamStart = birthOrigin + from;
                runtime.beamEnd = birthOrigin + to;
            }
            const kb::math::Vec3 d = runtime.beamEnd - runtime.beamStart;
            const kb::math::Vec3 axis = kb::math::Dot(d, d) > 1.0e-10F ? kb::math::Normalize(d) : kb::math::Vec3{0, 1, 0};
            const kb::math::Vec3 b1 = kb::math::Normalize(std::abs(axis.y) < 0.99F
                ? kb::math::Cross(axis, kb::math::Vec3{0.0F, 1.0F, 0.0F}) : kb::math::Cross(axis, kb::math::Vec3{1.0F, 0.0F, 0.0F}));
            const kb::math::Vec3 b2 = kb::math::Cross(axis, b1);
            const float t = static_cast<float>(pointOfBeam) / static_cast<float>(emitter.beam.segments);
            const float w = std::sin(t * kb::math::kPi) * 2.0F * emitter.beam.noise;
            particlePositions_.push_back(runtime.beamStart + d * t + b1 * ((NextRandom01(runtime) - 0.5F) * w) +
                                         b2 * ((NextRandom01(runtime) - 0.5F) * w));
            particlePreviousPositions_.push_back(particlePositions_.back());
            particleVelocities_.push_back({});
        } else {
            kb::math::Vec3 point, normal;
            sampleShape(point, normal);
            particlePositions_.push_back(birthOrigin + kb::math::Rotate(frame, point));
            particlePreviousPositions_.push_back(particlePositions_.back());
            particleVelocities_.push_back(kb::math::Rotate(frame, sampleVelocity(normal)) + emitter.addedVelocity);
        }
        particleAges_.push_back(0.0F);
        particleLifetimes_.push_back(lifetime);
        particleColors_.push_back(EvaluateGradient(emitter.colorOverLife, 0.0F));
        particleSizes_.push_back(emitter.sizeOverLife.keyCount > 0U
            ? EvaluateCurve(emitter.sizeOverLife, 0.0F) : 1.0F);
        particleEventDepths_.push_back(eventDepth);
        particlePrewarmGroups_.push_back(prewarmGroup);
        // Spin is drawn after everything else of the particle, and only from a real range, so effects that do not
        // use it consume exactly the random numbers they always did.
        const auto drawAxis = [this, &runtime](float low, float high) noexcept {
            return low != high ? low + (high - low) * NextRandom01(runtime) : low;
        };
        // Axis by axis, X then Y then Z, angles before velocities.
        kb::math::Vec3 spin{};
        spin.x = drawAxis(emitter.spinMinRadians.x, emitter.spinMaxRadians.x);
        spin.y = drawAxis(emitter.spinMinRadians.y, emitter.spinMaxRadians.y);
        spin.z = drawAxis(emitter.spinMinRadians.z, emitter.spinMaxRadians.z);
        kb::math::Vec3 spinRate{};
        spinRate.x = drawAxis(emitter.spinRateMinRadians.x, emitter.spinRateMaxRadians.x);
        spinRate.y = drawAxis(emitter.spinRateMinRadians.y, emitter.spinRateMaxRadians.y);
        spinRate.z = drawAxis(emitter.spinRateMinRadians.z, emitter.spinRateMaxRadians.z);
        particleSpins_.push_back(spin);
        particleSpinRates_.push_back(spinRate);
        // Drawn last, only from a real range, like the spin.
        particleSizeScales_.push_back(emitter.sizeRandom > 0.0F ? 1.0F - emitter.sizeRandom * NextRandom01(runtime) : 1.0F);
        static_cast<void>(QueueInternalEvent({
            .instanceId = instanceId,
            .sourceEmitterIndex = emitterIndex,
            .trigger = kb::scene::ParticleEventTrigger::Birth,
            .depth = eventDepth,
            .prewarmGroup = prewarmGroup,
            .position = particlePositions_.back(),
        }));
        ++runtime.spawnOrdinals[emitterIndex];
    }
    runtime.liveParticles[emitterIndex] += count * perBirth;
    return true;
}

void CpuParticleBackend::SpawnRequested(
    std::uint32_t denseIndex,
    std::uint8_t emitterIndex,
    std::uint32_t count,
    std::uint32_t& remainingSpawnBudget,
    std::uint8_t prewarmGroup) noexcept {
    stepTelemetry_.requestedSpawns += count;
    if (count == 0U) return;
    if (GpuEligible(denseIndex, emitterIndex, prewarmGroup)) {
        // GPU births are birth records the renderer's ring holds (as many as the emitter's capacity); they
        // do not take the CPU particles' spawns per step.
        SpawnGpu(denseIndex, emitterIndex, count, nullptr);
        stepTelemetry_.spawned += count;
        return;
    }
    const std::uint32_t withinBudget = std::min(count, remainingSpawnBudget);
    stepTelemetry_.rejectedByStepBudget += count - withinBudget;
    renderRejectedBySpawnBudget_[RenderGroupIndex(denseIndex, emitterIndex)] += count - withinBudget;
    remainingSpawnBudget -= withinBudget;
    if (withinBudget == 0U) return;
    InstanceRuntime& runtime = instanceRuntime_[denseIndex];
    const CompiledEmitter& emitter = compiledEffects_[runtime.compiledEffectIndex].effect->emitters[emitterIndex];
    // Capacities count points; a beam birth takes segments + 1 of them.
    const std::uint32_t perBirth = emitter.beam.enabled ? emitter.beam.segments + 1U : 1U;
    const std::uint32_t emitterSpace = (emitter.maxParticles - runtime.liveParticles[emitterIndex]) / perBirth;
    const std::uint64_t instanceId = MakeInstanceId(
        denseToSlot_[denseIndex], slotGenerations_[denseToSlot_[denseIndex]]);
    const std::uint32_t instanceLive = LiveParticleCount(instanceId);
    const std::uint32_t instanceLimit = InstanceParticleLimit(denseIndex);
    const std::uint32_t instanceSpace = (instanceLimit > instanceLive ? instanceLimit - instanceLive : 0U) / perBirth;
    const std::uint32_t sceneSpace = static_cast<std::uint32_t>(
        kb::scene::kParticleEffectMaxCpuParticlesPerScene - particleInstanceIds_.size()) / perBirth;
    const std::uint32_t accepted = std::min({withinBudget, emitterSpace, instanceSpace, sceneSpace});
    stepTelemetry_.rejectedByCapacity += withinBudget - accepted;
    renderRejectedByCapacity_[RenderGroupIndex(denseIndex, emitterIndex)] += withinBudget - accepted;
    if (accepted != 0U && SpawnExact(denseIndex, emitterIndex, accepted, 0U, prewarmGroup)) {
        stepTelemetry_.spawned += accepted;
    }
}

void CpuParticleBackend::UpdateSharedSimulations() noexcept {
    sharedLeaders_.assign(denseInstanceCount_, kInvalidDenseIndex);
    const auto shareable = [this](std::uint32_t d) noexcept {
        const CompiledEffect& effect = *compiledEffects_[instanceRuntime_[d].compiledEffectIndex].effect;
        if (playbackStates_[d] != PlaybackState::Playing || (!effect.looping && effect.durationSeconds > 0.0F) ||
            effect.eventBindingCount != 0U ||
            executionPaths_[d] == kb::particles::ParticleRuntimeExecutionPath::GpuVisual) return false;
        for (std::uint8_t e = 0U; e < effect.emitterCount; ++e) {
            if (effect.emitters[e].enabled && effect.emitters[e].simulationSpace != kb::scene::ParticleSimulationSpace::Local) return false;
        }
        const std::uint64_t id = MakeInstanceId(denseToSlot_[d], slotGenerations_[denseToSlot_[d]]);
        return std::none_of(parameters_.begin(), parameters_.end(), [id](const ParameterEntry& p) { return p.instanceId == id; });
    };
    for (std::uint32_t copy = 0U; copy < denseInstanceCount_; ++copy) {
        if (!shareable(copy)) continue;
        for (std::uint32_t leader = 0U; leader < copy; ++leader) {
            if (sharedLeaders_[leader] == kInvalidDenseIndex &&
                instanceRuntime_[leader].compiledEffectIndex == instanceRuntime_[copy].compiledEffectIndex &&
                seeds_[leader] == seeds_[copy] && rateMultipliers_[leader] == rateMultipliers_[copy] &&
                maxParticlesOverrides_[leader] == maxParticlesOverrides_[copy] && shareable(leader)) {
                sharedLeaders_[copy] = leader;
                break;
            }
        }
    }
}

void CpuParticleBackend::StepInstance(
    std::uint32_t denseIndex,
    float fixedDeltaSeconds,
    std::uint32_t& remainingSpawnBudget) noexcept {
    InstanceRuntime& runtime = instanceRuntime_[denseIndex];
    const CompiledEffect& effect = *compiledEffects_[runtime.compiledEffectIndex].effect;
    const bool finiteDuration = !effect.looping && effect.durationSeconds > 0.0F;
    bool canEmit = false;
    for (std::uint8_t emitterIndex = 0U; emitterIndex < effect.emitterCount; ++emitterIndex) {
        if (!effect.emitters[emitterIndex].enabled) continue;
        if (!finiteDuration || runtime.elapsedSeconds[emitterIndex] < effect.durationSeconds) {
            canEmit = true;
            StepEmitter(denseIndex, emitterIndex, fixedDeltaSeconds, remainingSpawnBudget);
        }
    }
    if (finiteDuration && !canEmit) {
        const bool hasLiveParticles = std::any_of(runtime.liveParticles.begin(), runtime.liveParticles.end(),
            [](std::uint32_t count) { return count != 0U; });
        playbackStates_[denseIndex] = hasLiveParticles ? PlaybackState::Draining : PlaybackState::Stopped;
    }
}

void CpuParticleBackend::StepEmitter(
    std::uint32_t denseIndex,
    std::uint8_t emitterIndex,
    float fixedDeltaSeconds,
    std::uint32_t& remainingSpawnBudget,
    std::uint8_t prewarmGroup) noexcept {
    InstanceRuntime& runtime = instanceRuntime_[denseIndex];
    const CompiledEffect& effect = *compiledEffects_[runtime.compiledEffectIndex].effect;
    const CompiledEmitter& emitter = effect.emitters[emitterIndex];
    if (!emitter.enabled) return;
    const bool finiteDuration = !effect.looping && effect.durationSeconds > 0.0F;
    const float previous = runtime.elapsedSeconds[emitterIndex];
    const float activeDelta = finiteDuration
        ? std::min(fixedDeltaSeconds, std::max(0.0F, effect.durationSeconds - previous))
        : fixedDeltaSeconds;
    if (activeDelta <= 0.0F) return;
    // The emitter's own clock: nothing before its start delay; its cycle (the effect's duration and
    // looping unless the emitter has one) repeats the rate and the bursts, or runs once.
    const float cycle = emitter.cycleSeconds > 0.0F ? emitter.cycleSeconds
        : (effect.looping && effect.durationSeconds > 0.0F ? effect.durationSeconds : 0.0F);
    const bool cycleLoops = emitter.cycleSeconds > 0.0F ? emitter.cycleLooping : effect.looping;
    // The run time of this step, cut at the start delay and at the end of a single cycle (kept as the
    // step itself when nothing is cut, so the rate integrates exactly).
    float runPrevious = previous - emitter.startDelaySeconds;
    float runDelta = activeDelta;
    if (runPrevious < 0.0F) {
        runDelta = runPrevious + activeDelta;
        runPrevious = 0.0F;
    }
    if (!cycleLoops && cycle > 0.0F && runPrevious + runDelta > cycle) runDelta = cycle - runPrevious;
    if (runDelta <= 0.0F) {
        runtime.elapsedSeconds[emitterIndex] += activeDelta;
        return;
    }
    const float runCurrent = runPrevious + runDelta;
    float localPrevious = runPrevious;
    float localCurrent = runCurrent;
    bool wrapped = false;
    if (cycleLoops && cycle > 0.0F) {
        localPrevious = std::fmod(runPrevious, cycle);
        localCurrent = localPrevious + runDelta;
        if (localCurrent >= cycle) {
            localCurrent = std::fmod(localCurrent, cycle);
            wrapped = true;
        }
    }

    if (wrapped) runtime.nextBurst[emitterIndex] = 0U;
    if (emitter.mode == kb::scene::ParticleSpawnMode::Continuous) {
        const float sampleTime = wrapped ? localCurrent * 0.5F : (localPrevious + localCurrent) * 0.5F;
        runtime.emissionFractions[emitterIndex] +=
            std::max(0.0F, EvaluateRate(emitter, sampleTime)) * rateMultipliers_[denseIndex] * runDelta;
        const std::uint32_t continuous = static_cast<std::uint32_t>(runtime.emissionFractions[emitterIndex]);
        runtime.emissionFractions[emitterIndex] -= static_cast<float>(continuous);
        SpawnRequested(denseIndex, emitterIndex, continuous, remainingSpawnBudget, prewarmGroup);
    }
    auto fireRange = [&](float begin, float end, bool includeZero) noexcept {
        while (runtime.nextBurst[emitterIndex] < emitter.burstCount) {
            const kb::scene::ParticleBurstAsset& burst = emitter.bursts[runtime.nextBurst[emitterIndex]];
            if (burst.timeSeconds > end) break;
            ++runtime.nextBurst[emitterIndex];
            if ((includeZero ? burst.timeSeconds >= begin : burst.timeSeconds > begin)) {
                SpawnRequested(denseIndex, emitterIndex, burst.count, remainingSpawnBudget, prewarmGroup);
            }
        }
    };
    if (wrapped) {
        fireRange(localPrevious, cycle, false);
        runtime.nextBurst[emitterIndex] = 0U;
        fireRange(0.0F, localCurrent, true);
    } else {
        fireRange(localPrevious, localCurrent, !runtime.cycleStarted[emitterIndex]);
    }
    runtime.cycleStarted[emitterIndex] = true;
    runtime.elapsedSeconds[emitterIndex] += activeDelta;
}

void CpuParticleBackend::AdvanceParticleAges(
    float fixedDeltaSeconds,
    std::uint64_t onlyInstanceId,
    std::uint8_t onlyPrewarmGroup) noexcept {
    std::size_t index = 0U;
    while (index < particleAges_.size()) {
        if (onlyInstanceId != 0U && particleInstanceIds_[index] != onlyInstanceId) {
            ++index;
            continue;
        }
        if (onlyPrewarmGroup != UINT8_MAX && particlePrewarmGroups_[index] != onlyPrewarmGroup) {
            ++index;
            continue;
        }
        const std::uint32_t ownerDenseIndex = ResolveDenseIndex(particleInstanceIds_[index]);
        if (ownerDenseIndex == kInvalidDenseIndex ||
            (playbackStates_[ownerDenseIndex] != PlaybackState::Playing &&
             playbackStates_[ownerDenseIndex] != PlaybackState::Draining)) {
            ++index;
            continue;
        }
        particleAges_[index] += fixedDeltaSeconds;
        if (particleAges_[index] < particleLifetimes_[index]) {
            ++index;
            continue;
        }
        const std::uint64_t instanceId = particleInstanceIds_[index];
        const std::uint8_t emitterIndex = particleEmitterIndices_[index];
        static_cast<void>(QueueInternalEvent({
            .instanceId = instanceId,
            .sourceEmitterIndex = emitterIndex,
            .trigger = kb::scene::ParticleEventTrigger::Death,
            .depth = particleEventDepths_[index],
            .prewarmGroup = particlePrewarmGroups_[index],
            .position = particlePositions_[index],
        }));
        const std::uint32_t denseIndex = ResolveDenseIndex(instanceId);
        if (denseIndex != kInvalidDenseIndex && instanceRuntime_[denseIndex].liveParticles[emitterIndex] != 0U) {
            --instanceRuntime_[denseIndex].liveParticles[emitterIndex];
            const bool empty = std::all_of(instanceRuntime_[denseIndex].liveParticles.begin(),
                instanceRuntime_[denseIndex].liveParticles.end(), [](std::uint32_t count) { return count == 0U; });
            if (empty && playbackStates_[denseIndex] == PlaybackState::Draining) {
                playbackStates_[denseIndex] = PlaybackState::Stopped;
            }
        }
        const std::size_t last = particleAges_.size() - 1U;
        particleInstanceIds_[index] = particleInstanceIds_[last];
        particleIds_[index] = particleIds_[last];
        particleSpawnOrdinals_[index] = particleSpawnOrdinals_[last];
        particleRibbonGroups_[index] = particleRibbonGroups_[last];
        particleEmitterIndices_[index] = particleEmitterIndices_[last];
        particlePositions_[index] = particlePositions_[last];
        particlePreviousPositions_[index] = particlePreviousPositions_[last];
        particleVelocities_[index] = particleVelocities_[last];
        particleAges_[index] = particleAges_[last];
        particleLifetimes_[index] = particleLifetimes_[last];
        particleColors_[index] = particleColors_[last];
        particleSizes_[index] = particleSizes_[last];
        particleSpins_[index] = particleSpins_[last];
        particleSpinRates_[index] = particleSpinRates_[last];
        particleSizeScales_[index] = particleSizeScales_[last];
        particleEventDepths_[index] = particleEventDepths_[last];
        particlePrewarmGroups_[index] = particlePrewarmGroups_[last];
        particleInstanceIds_.pop_back();
        particleIds_.pop_back();
        particleSpawnOrdinals_.pop_back();
        particleRibbonGroups_.pop_back();
        particleEmitterIndices_.pop_back();
        particlePositions_.pop_back();
        particlePreviousPositions_.pop_back();
        particleVelocities_.pop_back();
        particleAges_.pop_back();
        particleLifetimes_.pop_back();
        particleColors_.pop_back();
        particleSizes_.pop_back();
        particleSpins_.pop_back();
        particleSpinRates_.pop_back();
        particleSizeScales_.pop_back();
        particleEventDepths_.pop_back();
        particlePrewarmGroups_.pop_back();
        ++stepTelemetry_.deaths;
    }
}

void CpuParticleBackend::ExecuteForcesAndIntegrate(
    float fixedDeltaSeconds,
    std::uint64_t onlyInstanceId,
    std::uint8_t onlyPrewarmGroup) noexcept {
    // The emitter a particle integrates under this step, null when it does not.
    const auto emitterOf = [&](std::size_t index) noexcept -> const CompiledEmitter* {
        if (onlyInstanceId != 0U && particleInstanceIds_[index] != onlyInstanceId) return nullptr;
        if (onlyPrewarmGroup != UINT8_MAX && particlePrewarmGroups_[index] != onlyPrewarmGroup) return nullptr;
        const std::uint32_t denseIndex = ResolveDenseIndex(particleInstanceIds_[index]);
        if (denseIndex == kInvalidDenseIndex ||
            (playbackStates_[denseIndex] != PlaybackState::Playing &&
             playbackStates_[denseIndex] != PlaybackState::Draining)) {
            return nullptr;
        }
        return &compiledEffects_[instanceRuntime_[denseIndex].compiledEffectIndex].effect->emitters[particleEmitterIndices_[index]];
    };
    // Separation: a hashed grid of each emitter's particles (cell = its distance), the push from
    // neighbours of the same instance and emitter (Verth's Separation card).
    thread_local std::vector<std::uint32_t> cellHeads;
    thread_local std::vector<std::uint32_t> cellNext;
    thread_local std::vector<kb::math::Vec3> separationPush;
    const std::size_t particleCount = particleInstanceIds_.size();
    const auto separationOf = [&](std::size_t index) noexcept -> const kb::scene::ParticleSeparationModule* {
        const CompiledEmitter* owner = emitterOf(index);
        if (owner == nullptr) return nullptr;
        for (std::uint8_t m = 0U; m < owner->moduleCount; ++m) {
            if (owner->modules[m].enabled && owner->modules[m].type == kb::scene::ParticleModuleType::Separation)
                return &std::get<kb::scene::ParticleSeparationModule>(owner->modules[m].payload);
        }
        return nullptr;
    };
    constexpr std::uint32_t kSeparationCells = 4096U;
    const auto cellOf = [&](std::size_t index, float distance, int dx, int dy, int dz) noexcept {
        const auto c = [distance](float v, int d) {
            return static_cast<std::uint32_t>(static_cast<std::int32_t>(std::floor(v / distance)) + d);
        };
        const kb::math::Vec3 p = particlePositions_[index];
        std::uint32_t h = c(p.x, dx) * 0x8DA6B343U ^ c(p.y, dy) * 0xD8163841U ^ c(p.z, dz) * 0xCB1AB31FU ^
            static_cast<std::uint32_t>(particleInstanceIds_[index] * 0x9E3779B1U) ^ particleEmitterIndices_[index] * 0x85EBCA6BU;
        h ^= h >> 15U;
        return h & (kSeparationCells - 1U);
    };
    cellHeads.assign(kSeparationCells, UINT32_MAX);
    cellNext.assign(particleCount, UINT32_MAX);
    separationPush.assign(particleCount, kb::math::Vec3{});
    for (std::size_t index = 0U; index < particleCount; ++index) {
        if (const auto* separation = separationOf(index)) {
            const std::uint32_t cell = cellOf(index, separation->distance, 0, 0, 0);
            cellNext[index] = cellHeads[cell];
            cellHeads[cell] = static_cast<std::uint32_t>(index);
        }
    }
    for (std::size_t index = 0U; index < particleCount; ++index) {
        const auto* separation = separationOf(index);
        if (separation == nullptr) continue;
        std::uint32_t visited[27];
        std::uint32_t visitedCount = 0U;
        kb::math::Vec3 push{};
        for (int dz = -1; dz <= 1; ++dz) for (int dy = -1; dy <= 1; ++dy) for (int dx = -1; dx <= 1; ++dx) {
            const std::uint32_t cell = cellOf(index, separation->distance, dx, dy, dz);
            if (std::find(visited, visited + visitedCount, cell) != visited + visitedCount) continue;
            visited[visitedCount++] = cell;
            for (std::uint32_t other = cellHeads[cell]; other != UINT32_MAX; other = cellNext[other]) {
                if (other == index || particleInstanceIds_[other] != particleInstanceIds_[index] ||
                    particleEmitterIndices_[other] != particleEmitterIndices_[index]) continue;
                const kb::math::Vec3 away = particlePositions_[index] - particlePositions_[other];
                const float d = std::sqrt(kb::math::Dot(away, away));
                if (d >= separation->distance || d < 1.0e-6F) continue;
                push = push + away * ((1.0F - d / separation->distance) / d);
            }
        }
        separationPush[index] = push * separation->strength;
    }
    for (std::size_t index = 0U; index < particleCount; ++index) {
        const CompiledEmitter* integrated = emitterOf(index);
        if (integrated == nullptr) continue;
        const std::uint32_t denseIndex = ResolveDenseIndex(particleInstanceIds_[index]);
        const CompiledEmitter& emitter = *integrated;
        kb::math::Vec3& velocity = particleVelocities_[index];
        const float normalizedAge = kb::math::Clamp(
            particleAges_[index] / particleLifetimes_[index], 0.0F, 1.0F);
        // Fields anchored to the emitter (axis rotation, attraction, torus) turn about where it is now.
        const kb::math::Vec3 origin = EmitterOrigin(denseIndex, emitter);
        kb::math::Color color = EvaluateGradient(emitter.colorOverLife, normalizedAge);
        float size = emitter.sizeOverLife.keyCount > 0U
            ? EvaluateCurve(emitter.sizeOverLife, normalizedAge) : 1.0F;
        float alphaMultiplier = 1.0F;
        for (std::uint8_t moduleIndex = 0U; moduleIndex < emitter.moduleCount; ++moduleIndex) {
            const CompiledEmitter::Module& module = emitter.modules[moduleIndex];
            if (!module.enabled) continue;
            switch (module.type) {
            case kb::scene::ParticleModuleType::InitialVelocity:
                break;
            case kb::scene::ParticleModuleType::Gravity: {
                const kb::scene::ParticleGravityModule& gravity =
                    std::get<kb::scene::ParticleGravityModule>(module.payload);
                velocity = velocity +
                    (gravity.acceleration + kb::scene::kParticleEffectDefaultSceneGravity *
                        gravity.sceneGravityScale) * fixedDeltaSeconds;
                break;
            }
            case kb::scene::ParticleModuleType::Wind:
                velocity = velocity + std::get<kb::scene::ParticleWindModule>(module.payload).acceleration *
                    fixedDeltaSeconds;
                break;
            case kb::scene::ParticleModuleType::Drag:
                velocity = velocity * std::exp(
                    -std::get<kb::scene::ParticleDragModule>(module.payload).coefficient * fixedDeltaSeconds);
                break;
            case kb::scene::ParticleModuleType::AlphaOverLife:
                alphaMultiplier = EvaluateCurve(emitter.alphaOverLife, normalizedAge);
                break;
            case kb::scene::ParticleModuleType::Noise: {
                const auto& noise = std::get<kb::scene::ParticleNoiseModule>(module.payload);
                // The field lies in the world: positions relative to the simulation origin are moved back.
                const kb::math::Vec3 world = particlePositions_[index] + kb::math::ToVec3(simulationOrigin_);
                velocity = velocity + CurlNoise(world * noise.frequency, static_cast<std::uint32_t>(module.moduleId)) *
                    (noise.strength * fixedDeltaSeconds);
                break;
            }
            case kb::scene::ParticleModuleType::AxisRotation: {
                const auto& rotation = std::get<kb::scene::ParticleAxisRotationModule>(module.payload);
                const float angle = rotation.degreesPerSecond * 0.017453292F * fixedDeltaSeconds;
                particlePositions_[index] = origin + RotateAbout(particlePositions_[index] - origin, rotation.axis, angle);
                velocity = RotateAbout(velocity, rotation.axis, angle);
                break;
            }
            case kb::scene::ParticleModuleType::Attraction: {
                const auto& attraction = std::get<kb::scene::ParticleAttractionModule>(module.payload);
                const kb::math::Vec3 to = origin + attraction.point - particlePositions_[index];
                const float distance = std::sqrt(kb::math::Dot(to, to));
                const bool everywhere = attraction.radius <= 0.0F;
                if (distance > 1.0e-4F && (everywhere || distance < attraction.radius)) {
                    velocity = velocity + to * (attraction.strength *
                        (everywhere ? 1.0F : 1.0F - distance / attraction.radius) / distance * fixedDeltaSeconds);
                }
                break;
            }
            case kb::scene::ParticleModuleType::TorusSwirl: {
                const auto& torus = std::get<kb::scene::ParticleTorusSwirlModule>(module.payload);
                const kb::math::Vec3 center = origin + torus.center;
                kb::math::Vec3 radial{particlePositions_[index].x - center.x, 0.0F, particlePositions_[index].z - center.z};
                const float flat = std::sqrt(kb::math::Dot(radial, radial));
                radial = flat > 1.0e-5F ? radial * (1.0F / flat) : kb::math::Vec3{1.0F, 0.0F, 0.0F};
                const kb::math::Vec3 ring = center + radial * torus.radius;
                const kb::math::Vec3 tangent{radial.z, 0.0F, -radial.x};
                particlePositions_[index] = ring + RotateAbout(particlePositions_[index] - ring, tangent,
                    torus.degreesPerSecond * 0.017453292F * fixedDeltaSeconds);
                break;
            }
            case kb::scene::ParticleModuleType::Separation:
                velocity = velocity + separationPush[index] * fixedDeltaSeconds;
                break;
            default:
                break;
            }
        }
        color.a *= alphaMultiplier;
        particleColors_[index] = color;
        particleSizes_[index] = size * particleSizeScales_[index];
        particlePositions_[index] = particlePositions_[index] + velocity * fixedDeltaSeconds;
        for (std::uint8_t moduleIndex = 0U; moduleIndex < emitter.moduleCount; ++moduleIndex) {
            const CompiledEmitter::Module& module = emitter.modules[moduleIndex];
            if (!module.enabled || module.type != kb::scene::ParticleModuleType::CollisionPlane) continue;
            const kb::scene::ParticleCollisionPlaneModule& collision =
                std::get<kb::scene::ParticleCollisionPlaneModule>(module.payload);
            const float signedDistance = kb::math::Dot(collision.normal, particlePositions_[index]) -
                PlaneDistance(collision);
            if (signedDistance >= 0.0F) continue;
            particlePositions_[index] = particlePositions_[index] - collision.normal * signedDistance;
            const float normalVelocity = kb::math::Dot(velocity, collision.normal);
            if (normalVelocity < 0.0F) {
                const kb::math::Vec3 tangent = velocity - collision.normal * normalVelocity;
                velocity = tangent * (1.0F - collision.friction) -
                    collision.normal * (normalVelocity * collision.restitution);
            }
            ++stepTelemetry_.collisions;
            const std::size_t collisionBudgetIndex =
                (static_cast<std::size_t>(denseIndex) * kb::scene::kParticleEffectMaxEmitters +
                    particleEmitterIndices_[index]) * kb::scene::kParticleEffectMaxModulesPerEmitter + moduleIndex;
            if (collisionEventsThisStep_[collisionBudgetIndex] >= collision.maxEventsPerStep) {
                if (collision.killOnContact) particleAges_[index] = particleLifetimes_[index];
                ++stepTelemetry_.rejectedByEventBudget;
                ++renderRejectedByEventBudget_[RenderGroupIndex(denseIndex, particleEmitterIndices_[index])];
                eventActionBudgetExceeded_ = true;
                continue;
            }
            ++collisionEventsThisStep_[collisionBudgetIndex];
            static_cast<void>(QueueInternalEvent({
                .instanceId = particleInstanceIds_[index],
                .sourceEmitterIndex = particleEmitterIndices_[index],
                .sourceModuleId = module.moduleId,
                .trigger = kb::scene::ParticleEventTrigger::Collision,
                .depth = particleEventDepths_[index],
                .prewarmGroup = particlePrewarmGroups_[index],
                .position = particlePositions_[index],
            }));
            // Dies where it struck: its life ends, the next step removes it with its death event.
            if (collision.killOnContact) particleAges_[index] = particleLifetimes_[index];
        }
    }
}

void CpuParticleBackend::EmitWhileAlive(float fixedDeltaSeconds, std::uint32_t& remainingSpawnBudget) noexcept {
    // Only the particles alive at the start: the births join the arrays as the loop runs.
    const std::size_t alive = particleInstanceIds_.size();
    for (std::size_t index = 0U; index < alive; ++index) {
        const std::uint32_t denseIndex = ResolveDenseIndex(particleInstanceIds_[index]);
        if (denseIndex == kInvalidDenseIndex || (playbackStates_[denseIndex] != PlaybackState::Playing &&
                                                 playbackStates_[denseIndex] != PlaybackState::Draining)) {
            continue;
        }
        const CompiledEffect& effect = *compiledEffects_[instanceRuntime_[denseIndex].compiledEffectIndex].effect;
        for (std::uint8_t bindingIndex = 0U; bindingIndex < effect.eventBindingCount; ++bindingIndex) {
            const CompiledEffect::EventBinding& binding = effect.eventBindings[bindingIndex];
            if (binding.trigger != kb::scene::ParticleEventTrigger::WhileAlive ||
                binding.sourceEmitterIndex != particleEmitterIndices_[index] ||
                particleEventDepths_[index] >= binding.maxDepth) {
                continue;
            }
            // Births over this step at the binding's rate, phased per particle so neighbours do not
            // spawn in lockstep.
            const float phase = static_cast<float>((particleIds_[index] * 0x9E3779B97F4A7C15ULL) >> 40U) / 16777216.0F;
            const float age = particleAges_[index];
            const float births = std::floor(age * binding.ratePerSecond + phase) -
                std::floor(std::max(age - fixedDeltaSeconds, 0.0F) * binding.ratePerSecond + phase);
            if (births < 1.0F) continue;
            const std::uint32_t count = std::min(static_cast<std::uint32_t>(births), remainingSpawnBudget);
            remainingSpawnBudget -= count;
            const kb::math::Vec3 position = particlePositions_[index];
            if (count != 0U && SpawnExact(denseIndex, binding.targetEmitterIndex, count,
                    static_cast<std::uint8_t>(particleEventDepths_[index] + 1U), particlePrewarmGroups_[index], &position)) {
                stepTelemetry_.spawned += count;
            }
        }
    }
}

bool CpuParticleBackend::QueueInternalEvent(const InternalEvent& event) noexcept {
    if (!EventHasAction(event)) return true;
    std::vector<InternalEvent>& next = prewarmingInstanceId_ == event.instanceId
        ? prewarmNextEvents_ : nextEvents_;
    if (next.size() >= kb::scene::kParticleEffectMaxEventsPerStep) {
        ++stepTelemetry_.rejectedByEventBudget;
        const std::uint32_t denseIndex = ResolveDenseIndex(event.instanceId);
        if (denseIndex != kInvalidDenseIndex) {
            ++renderRejectedByEventBudget_[RenderGroupIndex(denseIndex, event.sourceEmitterIndex)];
        }
        eventQueueOverflowed_ = true;
        return false;
    }
    next.push_back(event);
    return true;
}

bool CpuParticleBackend::EventHasAction(const InternalEvent& event) const noexcept {
    const std::uint32_t denseIndex = ResolveDenseIndex(event.instanceId);
    if (denseIndex == kInvalidDenseIndex) return false;
    const CompiledEffect& effect = *compiledEffects_[instanceRuntime_[denseIndex].compiledEffectIndex].effect;
    const CompiledEmitter& emitter = effect.emitters[event.sourceEmitterIndex];
    const bool hasModuleAction = std::any_of(emitter.modules.begin(),
        emitter.modules.begin() + emitter.moduleCount, [&](const CompiledEmitter::Module& module) {
            const auto* sub = std::get_if<kb::scene::ParticleSubEmitterModule>(&module.payload);
            return module.enabled && module.type == kb::scene::ParticleModuleType::SubEmitter && sub != nullptr &&
                sub->trigger == event.trigger;
        });
    const bool hasBindingAction = std::any_of(effect.eventBindings.begin(),
        effect.eventBindings.begin() + effect.eventBindingCount, [&](const CompiledEffect::EventBinding& binding) {
            return binding.sourceEmitterIndex == event.sourceEmitterIndex && binding.trigger == event.trigger &&
                (binding.sourceModuleId == 0U || binding.sourceModuleId == event.sourceModuleId);
        });
    return hasModuleAction || hasBindingAction;
}

kb::particles::ParticleRuntimeStatus CpuParticleBackend::ProcessInternalEvents(
    std::uint32_t& remainingSpawnBudget) noexcept {
    const auto spawnAction = [&](std::uint32_t denseIndex, std::uint8_t emitterIndex, std::uint32_t count,
                                 const InternalEvent& event) {
        stepTelemetry_.requestedSpawns += count;
        const std::uint32_t withinBudget = std::min(count, remainingSpawnBudget);
        stepTelemetry_.rejectedByStepBudget += count - withinBudget;
        renderRejectedBySpawnBudget_[RenderGroupIndex(denseIndex, emitterIndex)] += count - withinBudget;
        remainingSpawnBudget -= withinBudget;
        if (withinBudget == 0U) return;
        InstanceRuntime& runtime = instanceRuntime_[denseIndex];
        const CompiledEmitter& target = compiledEffects_[runtime.compiledEffectIndex].effect->emitters[emitterIndex];
        const std::uint32_t emitterSpace = target.maxParticles - runtime.liveParticles[emitterIndex];
        const std::uint64_t instanceId = MakeInstanceId(
            denseToSlot_[denseIndex], slotGenerations_[denseToSlot_[denseIndex]]);
        const std::uint32_t instanceLive = LiveParticleCount(instanceId);
        const std::uint32_t instanceLimit = InstanceParticleLimit(denseIndex);
        const std::uint32_t instanceSpace = instanceLimit > instanceLive ? instanceLimit - instanceLive : 0U;
        const std::uint32_t sceneSpace = static_cast<std::uint32_t>(
            kb::scene::kParticleEffectMaxCpuParticlesPerScene - particleInstanceIds_.size());
        const std::uint32_t accepted = std::min({withinBudget, emitterSpace, instanceSpace, sceneSpace});
        stepTelemetry_.rejectedByCapacity += withinBudget - accepted;
        renderRejectedByCapacity_[RenderGroupIndex(denseIndex, emitterIndex)] += withinBudget - accepted;
        if (accepted != 0U && SpawnExact(denseIndex, emitterIndex, accepted,
                static_cast<std::uint8_t>(event.depth + 1U), event.prewarmGroup, &event.position)) {
            stepTelemetry_.spawned += accepted;
        }
    };

    std::vector<InternalEvent>& current = prewarmingInstanceId_ != 0U
        ? prewarmCurrentEvents_ : currentEvents_;
    for (std::size_t eventIndex = 0U; eventIndex < current.size(); ++eventIndex) {
        const InternalEvent event = current[eventIndex];
        const std::uint32_t denseIndex = ResolveDenseIndex(event.instanceId);
        if (denseIndex == kInvalidDenseIndex) continue;
        InstanceRuntime& runtime = instanceRuntime_[denseIndex];
        const CompiledEffect& effect = *compiledEffects_[runtime.compiledEffectIndex].effect;
        const CompiledEmitter& source = effect.emitters[event.sourceEmitterIndex];
        ++stepTelemetry_.processedEvents;
        for (std::uint8_t moduleIndex = 0U; moduleIndex < source.moduleCount; ++moduleIndex) {
            const CompiledEmitter::Module& module = source.modules[moduleIndex];
            if (!module.enabled || module.type != kb::scene::ParticleModuleType::SubEmitter) continue;
            const auto& sub = std::get<kb::scene::ParticleSubEmitterModule>(module.payload);
            if (sub.trigger != event.trigger || event.depth >= sub.maxDepth) continue;
            const auto target = std::find_if(effect.emitters.begin(), effect.emitters.begin() + effect.emitterCount,
                [&](const CompiledEmitter& emitter) { return emitter.emitterId == sub.targetEmitterId; });
            spawnAction(denseIndex, static_cast<std::uint8_t>(target - effect.emitters.begin()), sub.count, event);
        }
        for (std::uint8_t bindingIndex = 0U; bindingIndex < effect.eventBindingCount; ++bindingIndex) {
            const CompiledEffect::EventBinding& binding = effect.eventBindings[bindingIndex];
            if (binding.sourceEmitterIndex != event.sourceEmitterIndex || binding.trigger != event.trigger ||
                (binding.sourceModuleId != 0U && binding.sourceModuleId != event.sourceModuleId) ||
                event.depth >= binding.maxDepth) {
                continue;
            }
            if (runtime.bindingEventsThisStep[bindingIndex] >= binding.perStepBudget) {
                ++stepTelemetry_.rejectedByEventBudget;
                ++renderRejectedByEventBudget_[RenderGroupIndex(denseIndex, event.sourceEmitterIndex)];
                eventActionBudgetExceeded_ = true;
                continue;
            }
            ++runtime.bindingEventsThisStep[bindingIndex];
            spawnAction(denseIndex, binding.targetEmitterIndex, binding.count, event);
        }
    }
    current.clear();
    if (eventQueueOverflowed_) return kb::particles::ParticleRuntimeStatus::EventQueueFull;
    if (eventActionBudgetExceeded_) return kb::particles::ParticleRuntimeStatus::EventBudgetExceeded;
    if (stepTelemetry_.rejectedByStepBudget != 0U)
        return kb::particles::ParticleRuntimeStatus::SpawnBudgetExceeded;
    if (stepTelemetry_.rejectedByCapacity != 0U)
        return kb::particles::ParticleRuntimeStatus::ParticleCapacityReached;
    return kb::particles::ParticleRuntimeStatus::Success;
}

void CpuParticleBackend::BeginEventStep() noexcept {
    if (prewarmingInstanceId_ != 0U) {
        prewarmCurrentEvents_.swap(prewarmNextEvents_);
        prewarmNextEvents_.clear();
    } else {
        currentEvents_.swap(nextEvents_);
        nextEvents_.clear();
    }
    eventQueueOverflowed_ = false;
    eventActionBudgetExceeded_ = false;
    std::fill(collisionEventsThisStep_.begin(), collisionEventsThisStep_.end(), 0U);
    for (std::uint32_t denseIndex = 0U; denseIndex < denseInstanceCount_; ++denseIndex) {
        instanceRuntime_[denseIndex].bindingEventsThisStep.fill(0U);
    }
}

void CpuParticleBackend::RemoveQueuedEvents(std::uint64_t instanceId) noexcept {
    std::erase_if(currentEvents_, [&](const InternalEvent& event) { return event.instanceId == instanceId; });
    std::erase_if(nextEvents_, [&](const InternalEvent& event) { return event.instanceId == instanceId; });
    std::erase_if(prewarmCurrentEvents_, [&](const InternalEvent& event) { return event.instanceId == instanceId; });
    std::erase_if(prewarmNextEvents_, [&](const InternalEvent& event) { return event.instanceId == instanceId; });
}

void CpuParticleBackend::ApplyOwnerTransformDelta(
    std::uint32_t denseIndex,
    const kb::scene::WorldTransform& previous,
    const kb::scene::WorldTransform& current) noexcept {
    const std::uint64_t instanceId = MakeInstanceId(
        denseToSlot_[denseIndex], slotGenerations_[denseToSlot_[denseIndex]]);
    const CompiledEffect& effect = *compiledEffects_[instanceRuntime_[denseIndex].compiledEffectIndex].effect;
    for (std::size_t index = 0U; index < particleInstanceIds_.size(); ++index) {
        if (particleInstanceIds_[index] != instanceId ||
            effect.emitters[particleEmitterIndices_[index]].simulationSpace !=
                kb::scene::ParticleSimulationSpace::Local) {
            continue;
        }
        particlePositions_[index] = TransformPoint(current,
            InverseTransformPoint(previous, particlePositions_[index]));
        const kb::math::Vec3 localVelocity = Unscale(
            kb::math::Rotate(kb::math::Inverse(previous.rotation), particleVelocities_[index]), previous.scale);
        particleVelocities_[index] = TransformDirection(current, Scale(localVelocity, current.scale));
    }
    const auto moveEvents = [&](std::vector<InternalEvent>& events) noexcept {
        for (InternalEvent& event : events) {
            if (event.instanceId == instanceId &&
                effect.emitters[event.sourceEmitterIndex].simulationSpace ==
                    kb::scene::ParticleSimulationSpace::Local) {
                event.position = TransformPoint(current, InverseTransformPoint(previous, event.position));
            }
        }
    };
    moveEvents(currentEvents_);
    moveEvents(nextEvents_);
    moveEvents(prewarmCurrentEvents_);
    moveEvents(prewarmNextEvents_);
}

void CpuParticleBackend::CapturePreviousParticlePositions() noexcept {
    std::copy(particlePositions_.begin(), particlePositions_.end(), particlePreviousPositions_.begin());
}

kb::particles::ParticleRenderSnapshotResult CpuParticleBackend::PublishRenderSnapshot(
    kb::scene::Scene& scene,
    std::uint64_t fixedStepIndex) noexcept {
    std::fill(renderGroupCounts_.begin(), renderGroupCounts_.end(), 0U);
    for (std::size_t particleIndex = 0U; particleIndex < particleInstanceIds_.size(); ++particleIndex) {
        const std::uint32_t denseIndex = ResolveDenseIndex(particleInstanceIds_[particleIndex]);
        if (denseIndex == kInvalidDenseIndex) {
            return {kb::particles::ParticleRenderSnapshotStatus::InvalidSnapshot};
        }
        ++renderGroupCounts_[RenderGroupIndex(denseIndex, particleEmitterIndices_[particleIndex])];
    }
    // Copies sharing a simulation draw their leader's particles.
    UpdateSharedSimulations();
    std::uint32_t sharedParticles = 0U;
    for (std::uint32_t copy = 0U; copy < denseInstanceCount_; ++copy) {
        if (sharedLeaders_[copy] == kInvalidDenseIndex) continue;
        std::uint32_t leaderTotal = 0U;
        for (std::uint8_t e = 0U; e < kb::scene::kParticleEffectMaxEmitters; ++e)
            leaderTotal += renderGroupCounts_[RenderGroupIndex(sharedLeaders_[copy], e)];
        // Copies draw within the scene's particle limit, as separate simulations would.
        if (particleInstanceIds_.size() + sharedParticles + leaderTotal > kb::scene::kParticleEffectMaxCpuParticlesPerScene) {
            sharedLeaders_[copy] = kInvalidDenseIndex;
            continue;
        }
        for (std::uint8_t e = 0U; e < kb::scene::kParticleEffectMaxEmitters; ++e)
            renderGroupCounts_[RenderGroupIndex(copy, e)] += renderGroupCounts_[RenderGroupIndex(sharedLeaders_[copy], e)];
        sharedParticles += leaderTotal;
    }

    std::uint32_t particleOffset = 0U;
    std::size_t emitterRecordCount = 0U;
    for (std::uint32_t slot = 0U; slot < static_cast<std::uint32_t>(slotToDense_.size()); ++slot) {
        const std::uint32_t denseIndex = slotToDense_[slot];
        if (denseIndex == kInvalidDenseIndex) continue;
        const InstanceRuntime& runtime = instanceRuntime_[denseIndex];
        const CompiledEffectEntry& effectEntry = compiledEffects_[runtime.compiledEffectIndex];
        const CompiledEffect& effect = *effectEntry.effect;
        const std::uint64_t instanceId = MakeInstanceId(slot, slotGenerations_[slot]);
        kb::particles::ParticleRenderEmitterStatus playbackStatus =
            kb::particles::ParticleRenderEmitterStatus::Stopped;
        switch (playbackStates_[denseIndex]) {
        case PlaybackState::Playing:
            playbackStatus = kb::particles::ParticleRenderEmitterStatus::Playing;
            break;
        case PlaybackState::Paused:
            playbackStatus = kb::particles::ParticleRenderEmitterStatus::Paused;
            break;
        case PlaybackState::Draining:
            playbackStatus = kb::particles::ParticleRenderEmitterStatus::Draining;
            break;
        case PlaybackState::Stopped:
            break;
        }
        for (std::uint8_t emitterIndex = 0U; emitterIndex < effect.emitterCount; ++emitterIndex) {
            const std::size_t groupIndex = RenderGroupIndex(denseIndex, emitterIndex);
            const CompiledEmitter& emitter = effect.emitters[emitterIndex];
            const std::uint32_t count = renderGroupCounts_[groupIndex];
            const std::uint32_t rejectedCapacity = renderRejectedByCapacity_[groupIndex];
            const std::uint32_t rejectedSpawn = renderRejectedBySpawnBudget_[groupIndex];
            const std::uint32_t rejectedEvent = renderRejectedByEventBudget_[groupIndex];
            const std::uint64_t rejectedTotal = static_cast<std::uint64_t>(rejectedCapacity) +
                rejectedSpawn + rejectedEvent;
            kb::particles::ParticleRenderDropReason dropReason = kb::particles::ParticleRenderDropReason::None;
            if (rejectedEvent != 0U) dropReason = kb::particles::ParticleRenderDropReason::EventBudget;
            else if (rejectedSpawn != 0U) dropReason = kb::particles::ParticleRenderDropReason::SpawnBudget;
            else if (rejectedCapacity != 0U) dropReason = kb::particles::ParticleRenderDropReason::Capacity;
            const kb::math::Vec3 emptyBounds = TransformPoint(
                ownerTransforms_[denseIndex], emitter.localPosition);
            renderGroupWriteOffsets_[groupIndex] = particleOffset;
            renderGroupRecordIndices_[groupIndex] = static_cast<std::uint16_t>(emitterRecordCount);
            renderEmitterScratch_[emitterRecordCount++] = {
                .instanceId = instanceId,
                .effectAssetId = effectEntry.assetId,
                .emitterId = emitter.emitterId,
                // AssetManager generation zero is the canonical first load, while
                // render snapshots reserve zero for malformed/uninitialized data.
                .assetGeneration = effectEntry.assetGeneration == std::numeric_limits<std::uint64_t>::max()
                    ? effectEntry.assetGeneration
                    : effectEntry.assetGeneration + 1U,
                .materialAssetId = emitter.materialAssetId,
                .meshAssetId = emitter.meshAssetId,
                .textureAtlasAssetId = emitter.textureAtlasAssetId,
                .firstParticle = particleOffset,
                .particleCount = count,
                .liveParticleCount = count,
                .rejectedByCapacity = rejectedCapacity,
                .rejectedBySpawnBudget = rejectedSpawn,
                .rejectedByEventBudget = rejectedEvent,
                .droppedParticleCount = static_cast<std::uint32_t>(std::min<std::uint64_t>(
                    rejectedTotal, std::numeric_limits<std::uint32_t>::max())),
                .output = ToRenderOutput(emitter.outputType),
                .blend = ToRenderBlend(emitter.blendMode),
                .depth = ToRenderDepth(emitter.depthTest, emitter.depthWrite),
                .sort = ToRenderSort(emitter.sortMode),
                .status = emitter.enabled
                    ? playbackStatus
                    : kb::particles::ParticleRenderEmitterStatus::Stopped,
                .droppedReason = dropReason,
                .alignment = ToRenderAlignment(emitter.alignment),
                .flags = (emitter.softParticles
                    ? kb::particles::ParticleRenderEmitterFlag::SoftParticles
                    : kb::particles::ParticleRenderEmitterFlag::None) |
                    (emitter.antiAliasing
                        ? kb::particles::ParticleRenderEmitterFlag::AntiAliasing
                        : kb::particles::ParticleRenderEmitterFlag::None) |
                    (emitter.outputType == kb::scene::ParticleOutputType::Mesh && emitter.meshCastsShadow
                        ? kb::particles::ParticleRenderEmitterFlag::CastsShadow
                        : kb::particles::ParticleRenderEmitterFlag::None) |
                    (emitter.outputType == kb::scene::ParticleOutputType::Mesh && emitter.meshReceivesShadow
                        ? kb::particles::ParticleRenderEmitterFlag::ReceivesShadow
                        : kb::particles::ParticleRenderEmitterFlag::None),
                .backendPolicy = effect.backendPolicy,
                .flipbookColumnsEncoded = static_cast<std::uint8_t>(emitter.flipbookColumns),
                .flipbookRowsEncoded = static_cast<std::uint8_t>(emitter.flipbookRows),
                .localBasisQuaternionSnorm = PackQuaternion(kb::math::Normalize(
                    ownerTransforms_[denseIndex].rotation * emitter.localRotation)),
                .pointSpriteDiameter = emitter.pointSpriteDiameter,
                .meshLodLevel = static_cast<std::int8_t>(std::clamp(
                    std::lround(emitter.meshLodBias), -128L, 127L)),
                .trailSampleIntervalSeconds = emitter.trailSampleIntervalSeconds,
                .trailMinimumDistance = emitter.trailMinimumDistance,
                .trailMaxSamplesPerParticle = emitter.trailMaxSamplesPerParticle,
                .trailWidth = emitter.trailWidth,
                .ribbonMaxSegments = emitter.ribbonMaxSegments,
                .ribbonWidth = emitter.ribbonWidth,
                .ribbonBreakOnDeath = emitter.ribbonBreakOnDeath,
                .beamLocalEnd = emitter.beamLocalEnd,
                .beamSegments = emitter.beamSegments,
                .beamWidth = emitter.beamWidth,
                .beamNoiseAmplitude = emitter.beamNoiseAmplitude,
                .beamNoiseFrequency = emitter.beamNoiseFrequency,
                .volumetricDensity = emitter.volumetricDensity,
                .volumetricRadiusScale = emitter.volumetricRadiusScale,
                .volumetricLowQualitySteps = emitter.volumetricLowQualitySteps,
                .volumetricHighQualitySteps = emitter.volumetricHighQualitySteps,
                .colorIntensity = emitter.colorIntensity,
                .lightRadius = emitter.lightRadius,
                .lightIntensity = emitter.lightIntensity,
                .lightMaxCount = static_cast<std::uint16_t>(emitter.lightEnabled ? std::min(emitter.lightMaxCount, 256U) : 0U),
                .decalDepth = emitter.decalDepth,
                .outputOrigin = emptyBounds,
                .beamEnd = TransformPoint(ownerTransforms_[denseIndex],
                    emitter.localPosition + emitter.beamLocalEnd),
                .boundsMinimum = count == 0U
                    ? emptyBounds
                    : kb::math::Vec3{std::numeric_limits<float>::max(),
                          std::numeric_limits<float>::max(), std::numeric_limits<float>::max()},
                .boundsMaximum = count == 0U
                    ? emptyBounds
                    : kb::math::Vec3{std::numeric_limits<float>::lowest(),
                          std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest()},
            };
            particleOffset += count;
        }
    }
    if (particleOffset != static_cast<std::uint32_t>(particleInstanceIds_.size()) + sharedParticles) {
        return {kb::particles::ParticleRenderSnapshotStatus::InvalidSnapshot};
    }
    renderParticleScratch_.resize(particleOffset);

    for (std::size_t particleIndex = 0U; particleIndex < particleInstanceIds_.size(); ++particleIndex) {
        const std::uint32_t denseIndex = ResolveDenseIndex(particleInstanceIds_[particleIndex]);
        const std::uint8_t emitterIndex = particleEmitterIndices_[particleIndex];
        const std::size_t groupIndex = RenderGroupIndex(denseIndex, emitterIndex);
        const std::uint32_t destinationIndex = renderGroupWriteOffsets_[groupIndex]++;
        const CompiledEmitter& emitter =
            compiledEffects_[instanceRuntime_[denseIndex].compiledEffectIndex].effect->emitters[emitterIndex];
        std::uint16_t frame = 0U;
        if (emitter.flipbookFrameCount > 1U && emitter.flipbookFramesPerSecond > 0.0F) {
            const std::uint32_t unboundedFrame = static_cast<std::uint32_t>(
                particleAges_[particleIndex] * emitter.flipbookFramesPerSecond);
            frame = static_cast<std::uint16_t>(emitter.flipbookLooping
                ? unboundedFrame % emitter.flipbookFrameCount
                : std::min<std::uint32_t>(unboundedFrame, emitter.flipbookFrameCount - 1U));
        }
        const float speed = std::sqrt(kb::math::Dot(
            particleVelocities_[particleIndex], particleVelocities_[particleIndex]));
        renderParticleScratch_[destinationIndex] = {
            .position = particlePositions_[particleIndex],
            .size = particleSizes_[particleIndex],
            .previousPosition = particlePreviousPositions_[particleIndex],
            .rotationRadians = particleSpins_[particleIndex].z + particleSpinRates_[particleIndex].z * particleAges_[particleIndex],
            .velocity = particleVelocities_[particleIndex],
            .stretch = std::max(emitter.stretchMinimumLength, speed * emitter.stretchVelocityScale),
            .particleId = particleIds_[particleIndex],
            .spawnOrdinal = particleSpawnOrdinals_[particleIndex],
            .packedColor = PackColor(particleColors_[particleIndex]),
            .ribbonGroup = particleRibbonGroups_[particleIndex],
            .frame = frame,
            .normalizedAgeUnorm = static_cast<std::uint16_t>(std::lround(
                kb::math::Clamp(particleLifetimes_[particleIndex] > 0.0F
                        ? particleAges_[particleIndex] / particleLifetimes_[particleIndex]
                        : 0.0F,
                    0.0F,
                    1.0F) * 65535.0F)),
            .rotationXSnorm = kb::particles::PackParticleAngle(
                particleSpins_[particleIndex].x + particleSpinRates_[particleIndex].x * particleAges_[particleIndex]),
            .rotationYSnorm = kb::particles::PackParticleAngle(
                particleSpins_[particleIndex].y + particleSpinRates_[particleIndex].y * particleAges_[particleIndex]),
        };
        kb::particles::ParticleRenderEmitterRecord& batch =
            renderEmitterScratch_[renderGroupRecordIndices_[groupIndex]];
        const kb::math::Vec3& position = particlePositions_[particleIndex];
        batch.boundsMinimum.x = std::min(batch.boundsMinimum.x, position.x);
        batch.boundsMinimum.y = std::min(batch.boundsMinimum.y, position.y);
        batch.boundsMinimum.z = std::min(batch.boundsMinimum.z, position.z);
        batch.boundsMaximum.x = std::max(batch.boundsMaximum.x, position.x);
        batch.boundsMaximum.y = std::max(batch.boundsMaximum.y, position.y);
        batch.boundsMaximum.z = std::max(batch.boundsMaximum.z, position.z);
    }
    for (std::uint32_t copy = 0U; copy < denseInstanceCount_; ++copy) {
        const std::uint32_t leader = sharedLeaders_[copy];
        if (leader == kInvalidDenseIndex) continue;
        const kb::scene::WorldTransform& from = ownerTransforms_[leader];
        const kb::scene::WorldTransform& to = ownerTransforms_[copy];
        const kb::math::Quat turn = to.rotation * kb::math::Inverse(from.rotation);
        for (std::uint8_t e = 0U; e < kb::scene::kParticleEffectMaxEmitters; ++e) {
            const std::size_t leaderGroup = RenderGroupIndex(leader, e), copyGroup = RenderGroupIndex(copy, e);
            const std::uint32_t end = renderGroupWriteOffsets_[leaderGroup];
            if (renderGroupCounts_[leaderGroup] == 0U) continue;
            kb::particles::ParticleRenderEmitterRecord& batch = renderEmitterScratch_[renderGroupRecordIndices_[copyGroup]];
            for (std::uint32_t index = end - renderGroupCounts_[leaderGroup]; index < end; ++index) {
                kb::particles::ParticleRenderRecord record = renderParticleScratch_[index];
                record.position = TransformPoint(to, InverseTransformPoint(from, record.position));
                record.previousPosition = TransformPoint(to, InverseTransformPoint(from, record.previousPosition));
                record.velocity = kb::math::Rotate(turn, record.velocity);
                batch.boundsMinimum = {std::min(batch.boundsMinimum.x, record.position.x),
                    std::min(batch.boundsMinimum.y, record.position.y), std::min(batch.boundsMinimum.z, record.position.z)};
                batch.boundsMaximum = {std::max(batch.boundsMaximum.x, record.position.x),
                    std::max(batch.boundsMaximum.y, record.position.y), std::max(batch.boundsMaximum.z, record.position.z)};
                renderParticleScratch_[renderGroupWriteOffsets_[copyGroup]++] = record;
            }
        }
    }

    return kb::particles::ParticlePlayback::PublishRenderSnapshot(scene, *this, {
        .revision = ++renderSnapshotRevision_,
        .fixedStepIndex = fixedStepIndex,
        .origin = simulationOrigin_,
        .emitters = std::span<const kb::particles::ParticleRenderEmitterRecord>{
            renderEmitterScratch_.data(), emitterRecordCount},
        .particles = std::span<const kb::particles::ParticleRenderRecord>{
            renderParticleScratch_.data(), renderParticleScratch_.size()},
    });
}

kb::particles::ParticleRenderSnapshotResult CpuParticleBackend::PublishRenderTombstone(
    kb::scene::Scene& scene,
    std::uint64_t fixedStepIndex) noexcept {
    return kb::particles::ParticlePlayback::PublishRenderSnapshot(scene, *this, {
        .revision = ++renderSnapshotRevision_,
        .fixedStepIndex = fixedStepIndex,
        .tombstone = true,
        .origin = simulationOrigin_,
    });
}

bool CpuParticleBackend::GpuEligible(std::uint32_t denseIndex, std::uint8_t emitterIndex, std::uint8_t prewarmGroup) const noexcept {
    if (gpuScene_ == nullptr || prewarmGroup != UINT8_MAX || prewarmingInstanceId_ != 0U ||
        !kb::particles::ParticlePlayback::HasGpuEmitterConsumer(*gpuScene_)) {
        return false;
    }
    const CompiledEffect& effect = *compiledEffects_[instanceRuntime_[denseIndex].compiledEffectIndex].effect;
    if (effect.backendPolicy == kb::scene::ParticleBackendPolicy::CpuDeterministic || emitterIndex >= effect.emitterCount) {
        return false;
    }
    const CompiledEmitter& emitter = effect.emitters[emitterIndex];
    if ((emitter.simulationSpace != kb::scene::ParticleSimulationSpace::World &&
         emitter.simulationSpace != kb::scene::ParticleSimulationSpace::Local) ||
        (emitter.outputType != kb::scene::ParticleOutputType::Billboard &&
         emitter.outputType != kb::scene::ParticleOutputType::StretchedBillboard &&
         emitter.outputType != kb::scene::ParticleOutputType::Mesh &&
         emitter.outputType != kb::scene::ParticleOutputType::Trail) ||
        emitter.alignment == kb::scene::ParticleAlignment::Local || emitter.flipbookFrameCount > 1U ||
        emitter.maxParticles > kb::particles::kParticleGpuMaxCapacity) {
        return false;
    }
    if (emitter.outputType == kb::scene::ParticleOutputType::Mesh && (emitter.meshAssetId == 0U || emitter.materialAssetId == 0U)) {
        return false;
    }
    // A trail follows the closed form back in time, which a colliding or local-space particle does not obey.
    const bool trail = emitter.outputType == kb::scene::ParticleOutputType::Trail;
    if (trail && (emitter.simulationSpace == kb::scene::ParticleSimulationSpace::Local || emitter.trailMaxSamplesPerParticle < 2U ||
        static_cast<std::uint64_t>(emitter.maxParticles) * (emitter.trailMaxSamplesPerParticle - 1U) > kb::particles::kParticleGpuMaxTrailSegments)) {
        return false;
    }
    const bool local = emitter.simulationSpace == kb::scene::ParticleSimulationSpace::Local;
    bool collisionPlane = false;
    bool forces = false;
    bool eventActions = false;
    std::uint8_t fieldTypes = 0U;
    for (std::uint8_t index = 0U; index < emitter.moduleCount; ++index) {
        const auto& module = emitter.modules[index];
        if (!module.enabled) continue;
        if (module.type == kb::scene::ParticleModuleType::CollisionPlane) {
            // The GPU plane only bounces; retain CPU simulation for contact deaths.
            if (local || trail || std::get<kb::scene::ParticleCollisionPlaneModule>(module.payload).killOnContact) return false;
            collisionPlane = true;
        } else if (module.type == kb::scene::ParticleModuleType::Noise ||
                   module.type == kb::scene::ParticleModuleType::AxisRotation ||
                   module.type == kb::scene::ParticleModuleType::Attraction) {
            // The stateful kernel integrates them; a trail follows the closed form, which they break.
            if (trail) return false;
            // The GPU parameter block holds one field of each type. Repeated fields must
            // execute in authored order on the CPU rather than overwrite an earlier field.
            const std::uint8_t fieldType = module.type == kb::scene::ParticleModuleType::Noise ? 1U :
                (module.type == kb::scene::ParticleModuleType::AxisRotation ? 2U : 4U);
            if ((fieldTypes & fieldType) != 0U) return false;
            fieldTypes |= fieldType;
            forces = true;
        } else if (module.type == kb::scene::ParticleModuleType::TorusSwirl ||
                   module.type == kb::scene::ParticleModuleType::Separation) {
            // A field the GPU kernel does not integrate: the CPU path follows it.
            return false;
        } else if (module.type == kb::scene::ParticleModuleType::SubEmitter) {
            // Collision events would have to come back from the GPU.
            if (std::get<kb::scene::ParticleSubEmitterModule>(module.payload).trigger == kb::scene::ParticleEventTrigger::Collision) {
                return false;
            }
            eventActions = true;
        }
    }
    if (emitter.beam.enabled || emitter.absolutePosition || emitter.shape.type == kb::scene::ParticleSpawnShape::Mesh) return false;
    for (std::uint8_t index = 0U; index < effect.eventBindingCount; ++index) {
        if (effect.eventBindings[index].sourceEmitterIndex != emitterIndex) continue;
        if (effect.eventBindings[index].trigger == kb::scene::ParticleEventTrigger::Collision ||
            effect.eventBindings[index].trigger == kb::scene::ParticleEventTrigger::WhileAlive) return false;
        eventActions = true;
    }
    // Death positions come from the closed form, which needs a free-flying world-space particle.
    return !(eventActions && (collisionPlane || forces || local));
}

namespace {

// The closed-form motion the GPU kernel evaluates (constant acceleration, linear drag), on the CPU: where a
// particle born at `start` with `velocity` is after `age` seconds.
[[nodiscard]] kb::math::Vec3 GpuClosedFormPosition(
    kb::math::Vec3 start, kb::math::Vec3 velocity, kb::math::Vec3 acceleration, float drag, float age) noexcept {
    if (drag > 0.0001F) {
        const kb::math::Vec3 terminal = acceleration * (1.0F / drag);
        const float decay = std::exp(-drag * age);
        return start + terminal * age + (velocity - terminal) * ((1.0F - decay) / drag);
    }
    return start + velocity * age + acceleration * (0.5F * age * age);
}

} // namespace

kb::particles::ParticleGpuEmitterParams CpuParticleBackend::BuildGpuParams(
    std::uint32_t denseIndex, std::uint8_t emitterIndex) const noexcept {
    const CompiledEffect& effect = *compiledEffects_[instanceRuntime_[denseIndex].compiledEffectIndex].effect;
    const CompiledEmitter& emitter = effect.emitters[emitterIndex];
    kb::particles::ParticleGpuEmitterParams params{};
    bool alphaOverLife = false;
    kb::math::Vec3 acceleration{};
    float drag = 0.0F;
    for (std::uint8_t index = 0U; index < emitter.moduleCount; ++index) {
        const auto& module = emitter.modules[index];
        if (!module.enabled) continue;
        if (module.type == kb::scene::ParticleModuleType::Gravity) {
            const auto& gravity = std::get<kb::scene::ParticleGravityModule>(module.payload);
            acceleration = acceleration + gravity.acceleration +
                kb::scene::kParticleEffectDefaultSceneGravity * gravity.sceneGravityScale;
        } else if (module.type == kb::scene::ParticleModuleType::Wind) {
            acceleration = acceleration + std::get<kb::scene::ParticleWindModule>(module.payload).acceleration;
        } else if (module.type == kb::scene::ParticleModuleType::Drag) {
            drag += std::get<kb::scene::ParticleDragModule>(module.payload).coefficient;
        } else if (module.type == kb::scene::ParticleModuleType::AlphaOverLife) {
            alphaOverLife = true;
        } else if (module.type == kb::scene::ParticleModuleType::CollisionPlane) {
            const auto& plane = std::get<kb::scene::ParticleCollisionPlaneModule>(module.payload);
            params.plane = {
                .normal = plane.normal, .distance = PlaneDistance(plane), .restitution = plane.restitution, .friction = plane.friction };
            params.hasPlane = true;
        } else if (module.type == kb::scene::ParticleModuleType::Noise) {
            const auto& noise = std::get<kb::scene::ParticleNoiseModule>(module.payload);
            params.noiseStrength = noise.strength;
            params.noiseFrequency = noise.frequency;
            params.noiseSeed = static_cast<std::uint32_t>(module.moduleId);
        } else if (module.type == kb::scene::ParticleModuleType::AxisRotation) {
            params.axisRadiansPerSecond =
                std::get<kb::scene::ParticleAxisRotationModule>(module.payload).degreesPerSecond * 0.017453292F;
        } else if (module.type == kb::scene::ParticleModuleType::Attraction) {
            const auto& attraction = std::get<kb::scene::ParticleAttractionModule>(module.payload);
            params.attractionStrength = attraction.strength;
            params.attractionRadius = attraction.radius;
        }
    }
    // An authored plane has no screen-depth semantics. Depth collision is a separate,
    // explicit renderer capability and must not depend on the presence of a plane.
    params.sceneDepthCollision = false;
    params.localSpace = emitter.simulationSpace == kb::scene::ParticleSimulationSpace::Local;
    params.acceleration = acceleration;
    params.drag = drag;
    for (std::size_t sample = 0U; sample < kb::particles::kParticleGpuCurveSamples; ++sample) {
        const float age = static_cast<float>(sample) / static_cast<float>(kb::particles::kParticleGpuCurveSamples - 1U);
        kb::math::Color color = EvaluateGradient(emitter.colorOverLife, age);
        if (alphaOverLife) color.a *= EvaluateCurve(emitter.alphaOverLife, age);
        // The emitter's colour intensity lifts the colour to HDR, as on the CPU path.
        params.color[sample] = { color.r * emitter.colorIntensity, color.g * emitter.colorIntensity,
            color.b * emitter.colorIntensity, color.a };
        params.size[sample] = emitter.sizeOverLife.keyCount > 0U ? EvaluateCurve(emitter.sizeOverLife, age) : 1.0F;
    }
    params.textureAtlasAssetId = emitter.textureAtlasAssetId;
    params.spinMin = emitter.spinMinRadians;
    params.spinMax = emitter.spinMaxRadians;
    params.spinRateMin = emitter.spinRateMinRadians;
    params.spinRateMax = emitter.spinRateMaxRadians;
    params.capacity = maxParticlesOverrides_[denseIndex] != 0U
        ? std::min(emitter.maxParticles, maxParticlesOverrides_[denseIndex]) : emitter.maxParticles;
    params.output = ToRenderOutput(emitter.outputType);
    params.blend = ToRenderBlend(emitter.blendMode);
    params.depth = ToRenderDepth(emitter.depthTest, emitter.depthWrite);
    params.alignment = ToRenderAlignment(emitter.alignment);
    params.softParticles = emitter.softParticles;
    params.stretchVelocityScale = emitter.stretchVelocityScale;
    params.stretchMinimumLength = emitter.stretchMinimumLength;
    if (emitter.outputType == kb::scene::ParticleOutputType::Mesh) {
        params.meshAssetId = emitter.meshAssetId;
        params.materialAssetId = emitter.materialAssetId;
        params.castsShadow = emitter.meshCastsShadow;
        params.receivesShadow = emitter.meshReceivesShadow;
    } else if (emitter.outputType == kb::scene::ParticleOutputType::Trail) {
        // The CPU path records a sample every `cadence` fixed steps; the closed form is sampled at the same spacing.
        const float stepsPerSecond = static_cast<float>(kb::scene::kParticleEffectFixedStepsPerSecond);
        params.trailSegments = emitter.trailMaxSamplesPerParticle - 1U;
        params.trailSegmentSeconds = std::max(1.0F, std::ceil(emitter.trailSampleIntervalSeconds * stepsPerSecond)) / stepsPerSecond;
        params.trailWidth = emitter.trailWidth;
    }
    return params;
}

void CpuParticleBackend::SpawnGpu(std::uint32_t denseIndex, std::uint8_t emitterIndex, std::uint32_t count,
    const kb::math::Vec3* eventPosition, std::uint8_t eventDepth) noexcept {
    InstanceRuntime& runtime = instanceRuntime_[denseIndex];
    const CompiledEmitter& emitter = compiledEffects_[runtime.compiledEffectIndex].effect->emitters[emitterIndex];
    const std::uint64_t instanceId = MakeInstanceId(denseToSlot_[denseIndex], slotGenerations_[denseToSlot_[denseIndex]]);
    GpuPending* pending = nullptr;
    for (GpuPending& candidate : gpuPending_) {
        if (candidate.instanceId == instanceId && candidate.emitterIndex == emitterIndex) pending = &candidate;
    }
    if (pending == nullptr) {
        gpuPending_.push_back(GpuPending{ .instanceId = instanceId, .denseIndex = denseIndex, .emitterIndex = emitterIndex });
        pending = &gpuPending_.back();
    }
    const kb::scene::WorldTransform& ownerTransform = ownerTransforms_[denseIndex];
    const kb::math::Vec3 emitterOffset = TransformDirection(ownerTransform,
        Scale(emitter.localPosition, ownerTransform.scale));
    // Birth and death events matter only when something is bound to them.
    InternalEvent eventPrototype{ .instanceId = instanceId, .sourceEmitterIndex = emitterIndex, .depth = eventDepth };
    eventPrototype.trigger = kb::scene::ParticleEventTrigger::Birth;
    const bool birthAction = EventHasAction(eventPrototype);
    eventPrototype.trigger = kb::scene::ParticleEventTrigger::Death;
    const bool deathAction = EventHasAction(eventPrototype);
    const kb::particles::ParticleGpuEmitterParams motion = deathAction ? BuildGpuParams(denseIndex, emitterIndex)
                                                                       : kb::particles::ParticleGpuEmitterParams{};
    const bool local = emitter.simulationSpace == kb::scene::ParticleSimulationSpace::Local;
    // A local-space record is in the owner's frame, so the world matrix sent each step puts it where the
    // world-space record would have been (the velocity matches the CPU path's rotation-only spawn velocity).
    const kb::math::Vec3 origin = local ? emitter.localPosition
        : (eventPosition != nullptr ? *eventPosition : ownerTransform.position) + emitterOffset;
    pending->spawns.reserve(pending->spawns.size() + count);
    for (std::uint32_t index = 0U; index < count; ++index) {
        const float lifetime = emitter.lifetimeMin + (emitter.lifetimeMax - emitter.lifetimeMin) * NextRandom01(runtime);
        // The same shape and start velocity as a CPU birth, turned by the emitter's rotation (and the
        // owner's, for world space).
        kb::math::Vec3 normal;
        const kb::math::Vec3 point = kb::math::Rotate(emitter.localRotation, SampleShape(emitter, runtime, normal));
        const kb::math::Vec3 sampled = kb::math::Rotate(emitter.localRotation, SampleStartVelocity(emitter, runtime, emitterIndex, normal));
        const kb::math::Vec3 velocity = (local ? Unscale(sampled, ownerTransform.scale) : TransformDirection(ownerTransform, sampled)) +
            emitter.addedVelocity;
        const kb::math::Vec3 position = origin + (local ? point : TransformDirection(ownerTransform, point));
        pending->spawns.push_back(kb::particles::ParticleGpuSpawn{
            .position = position, .birthTime = static_cast<float>(gpuSimTime_), .velocity = velocity, .lifetime = lifetime });
        ++runtime.spawnOrdinals[emitterIndex];
        eventPrototype.position = position;
        if (birthAction) {
            eventPrototype.trigger = kb::scene::ParticleEventTrigger::Birth;
            static_cast<void>(QueueInternalEvent(eventPrototype));
        }
        if (deathAction) {
            eventPrototype.trigger = kb::scene::ParticleEventTrigger::Death;
            eventPrototype.position = GpuClosedFormPosition(position, velocity, motion.acceleration, motion.drag, lifetime);
            ScheduleGpuDeath(eventPrototype, gpuSimTime_ + static_cast<double>(lifetime));
        }
    }
}

void CpuParticleBackend::ScheduleGpuDeath(const InternalEvent& event, double due) noexcept {
    // Pending deaths of a huge GPU emitter are bounded like the per-step event queue is.
    if (gpuScheduledEvents_.size() >= kb::scene::kParticleEffectMaxEventsPerStep * 64U) {
        ++stepTelemetry_.rejectedByEventBudget;
        eventQueueOverflowed_ = true;
        return;
    }
    gpuScheduledEvents_.push_back({ due, event });
    std::push_heap(gpuScheduledEvents_.begin(), gpuScheduledEvents_.end(),
        [](const ScheduledGpuEvent& lhs, const ScheduledGpuEvent& rhs) { return lhs.due > rhs.due; });
}

void CpuParticleBackend::ReleaseDueGpuEvents() noexcept {
    const auto later = [](const ScheduledGpuEvent& lhs, const ScheduledGpuEvent& rhs) { return lhs.due > rhs.due; };
    while (!gpuScheduledEvents_.empty() && gpuScheduledEvents_.front().due <= gpuSimTime_) {
        const InternalEvent event = gpuScheduledEvents_.front().event;
        std::pop_heap(gpuScheduledEvents_.begin(), gpuScheduledEvents_.end(), later);
        gpuScheduledEvents_.pop_back();
        static_cast<void>(QueueInternalEvent(event));
    }
}

void CpuParticleBackend::FlushGpuSpawns() noexcept {
    if (gpuScene_ == nullptr) {
        gpuPending_.clear();
        return;
    }
    for (GpuPending& pending : gpuPending_) {
        if (pending.denseIndex >= denseInstanceCount_) continue;
        const CompiledEffect& effect = *compiledEffects_[instanceRuntime_[pending.denseIndex].compiledEffectIndex].effect;
        kb::particles::ParticleGpuEmitterCommand command{};
        command.key = { pending.instanceId, effect.emitters[pending.emitterIndex].emitterId };
        command.simTime = gpuSimTime_;
        command.hasParams = true;
        command.params = BuildGpuParams(pending.denseIndex, pending.emitterIndex);
        command.spawns = std::move(pending.spawns);
        command.simulationOrigin = simulationOrigin_;
        kb::particles::ParticlePlayback::QueueGpuEmitterCommand(*gpuScene_, std::move(command));
    }
    gpuPending_.clear();
    // Local-space emitters follow their owner (world matrix) and mesh emitters orient their instances by it
    // (owner rotation times the emitter's own): refresh both of them each step.
    for (std::uint32_t denseIndex = 0U; denseIndex < denseInstanceCount_; ++denseIndex) {
        if (playbackStates_[denseIndex] == PlaybackState::Stopped) continue;
        const CompiledEffect& effect = *compiledEffects_[instanceRuntime_[denseIndex].compiledEffectIndex].effect;
        for (std::uint8_t emitterIndex = 0U; emitterIndex < effect.emitterCount; ++emitterIndex) {
            const CompiledEmitter& emitter = effect.emitters[emitterIndex];
            const bool local = emitter.simulationSpace == kb::scene::ParticleSimulationSpace::Local;
            const bool mesh = emitter.outputType == kb::scene::ParticleOutputType::Mesh;
            const bool field = std::any_of(emitter.modules.begin(), emitter.modules.begin() + emitter.moduleCount,
                [](const CompiledEmitter::Module& module) {
                    return module.enabled && (module.type == kb::scene::ParticleModuleType::AxisRotation ||
                        module.type == kb::scene::ParticleModuleType::Attraction);
                });
            if ((!local && !mesh && !field) || !GpuEligible(denseIndex, emitterIndex, UINT8_MAX)) {
                continue;
            }
            const kb::scene::WorldTransform& owner = ownerTransforms_[denseIndex];
            const kb::math::Vec3 axisX = kb::math::Rotate(owner.rotation, { owner.scale.x, 0.0F, 0.0F });
            const kb::math::Vec3 axisY = kb::math::Rotate(owner.rotation, { 0.0F, owner.scale.y, 0.0F });
            const kb::math::Vec3 axisZ = kb::math::Rotate(owner.rotation, { 0.0F, 0.0F, owner.scale.z });
            kb::particles::ParticleGpuEmitterCommand command{};
            command.key = { MakeInstanceId(denseToSlot_[denseIndex], slotGenerations_[denseToSlot_[denseIndex]]), effect.emitters[emitterIndex].emitterId };
            command.simTime = gpuSimTime_;
            command.simulationOrigin = simulationOrigin_;
            if (local) {
                command.hasWorldMatrix = true;
                command.worldMatrix = { axisX.x, axisX.y, axisX.z, 0.0F, axisY.x, axisY.y, axisY.z, 0.0F,
                    axisZ.x, axisZ.y, axisZ.z, 0.0F, owner.position.x, owner.position.y, owner.position.z, 1.0F };
            }
            if (mesh) {
                const kb::math::Quat basis = kb::math::Normalize(owner.rotation * effect.emitters[emitterIndex].localRotation);
                command.hasOrientation = true;
                command.orientation = { basis.x, basis.y, basis.z, basis.w };
                const kb::math::Vec3 origin = owner.position + TransformDirection(owner,
                    Scale(effect.emitters[emitterIndex].localPosition, owner.scale));
                command.origin = { origin.x, origin.y, origin.z };
            }
            if (field) {
                // The fields as the CPU modules place them (about the emitter's origin, the axis and point in world
                // axes), in the frame of the birth records: the owner's for a local-space emitter.
                const kb::math::Quat toRecords = local ? kb::math::Conjugate(owner.rotation) : kb::math::Quat{};
                command.hasField = true;
                command.fieldOrigin = local ? emitter.localPosition
                    : owner.position + TransformDirection(owner, Scale(emitter.localPosition, owner.scale));
                for (std::uint8_t moduleIndex = 0U; moduleIndex < emitter.moduleCount; ++moduleIndex) {
                    const CompiledEmitter::Module& module = emitter.modules[moduleIndex];
                    if (!module.enabled) continue;
                    if (module.type == kb::scene::ParticleModuleType::AxisRotation) {
                        command.fieldAxis = kb::math::Rotate(toRecords,
                            std::get<kb::scene::ParticleAxisRotationModule>(module.payload).axis);
                    } else if (module.type == kb::scene::ParticleModuleType::Attraction) {
                        command.attractionTarget = command.fieldOrigin + kb::math::Rotate(toRecords,
                            std::get<kb::scene::ParticleAttractionModule>(module.payload).point);
                    }
                }
            }
            kb::particles::ParticlePlayback::QueueGpuEmitterCommand(*gpuScene_, std::move(command));
        }
    }
}

void CpuParticleBackend::QueueGpuRelease(std::uint64_t instanceId, std::uint32_t denseIndex, bool release) noexcept {
    if (gpuScene_ == nullptr || !kb::particles::ParticlePlayback::HasGpuEmitterConsumer(*gpuScene_)) return;
    // Drop spawns of this instance that were produced but not yet flushed.
    std::erase_if(gpuPending_, [instanceId](const GpuPending& pending) { return pending.instanceId == instanceId; });
    if (std::erase_if(gpuScheduledEvents_, [instanceId](const ScheduledGpuEvent& scheduled) { return scheduled.event.instanceId == instanceId; }) != 0U) {
        std::make_heap(gpuScheduledEvents_.begin(), gpuScheduledEvents_.end(),
            [](const ScheduledGpuEvent& lhs, const ScheduledGpuEvent& rhs) { return lhs.due > rhs.due; });
    }
    const CompiledEffect& effect = *compiledEffects_[instanceRuntime_[denseIndex].compiledEffectIndex].effect;
    if (effect.backendPolicy == kb::scene::ParticleBackendPolicy::CpuDeterministic) return;
    for (std::uint8_t emitterIndex = 0U; emitterIndex < effect.emitterCount; ++emitterIndex) {
        kb::particles::ParticleGpuEmitterCommand command{};
        command.key = { instanceId, effect.emitters[emitterIndex].emitterId };
        command.simTime = gpuSimTime_;
        command.release = release;
        command.clear = !release;
        command.simulationOrigin = simulationOrigin_;
        kb::particles::ParticlePlayback::QueueGpuEmitterCommand(*gpuScene_, std::move(command));
    }
}

std::uint32_t CpuParticleBackend::InstanceParticleLimit(std::uint32_t denseIndex) const noexcept {
    if (maxParticlesOverrides_[denseIndex] != 0U) return maxParticlesOverrides_[denseIndex];
    const CompiledEffect& effect = *compiledEffects_[instanceRuntime_[denseIndex].compiledEffectIndex].effect;
    std::uint32_t limit = 0U;
    for (std::uint8_t emitterIndex = 0U; emitterIndex < effect.emitterCount; ++emitterIndex) {
        limit += effect.emitters[emitterIndex].maxParticles;
    }
    return std::min(limit, kb::scene::kParticleEffectMaxCpuParticlesPerScene);
}

std::uint32_t CpuParticleBackend::LiveParticleCount(std::uint64_t instanceId) const noexcept {
    // Per-emitter counters are kept in step with spawns, deaths and RemoveParticles, so this stays O(1)
    // instead of scanning every particle in the scene on each spawn request.
    const std::uint32_t denseIndex = ResolveDenseIndex(instanceId);
    if (denseIndex == kInvalidDenseIndex) return 0U;
    const auto& live = instanceRuntime_[denseIndex].liveParticles;
    return std::accumulate(live.begin(), live.end(), 0U);
}

} // namespace kb::particle_plugin
