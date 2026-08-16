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

#include <algorithm>
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
    std::uint64_t instanceId = 0U) noexcept {
    return { .status = status, .instanceId = instanceId };
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
    particleEmitterIndices_.reserve(kb::scene::kParticleEffectMaxCpuParticlesPerScene);
    particlePositions_.reserve(kb::scene::kParticleEffectMaxCpuParticlesPerScene);
    particlePreviousPositions_.reserve(kb::scene::kParticleEffectMaxCpuParticlesPerScene);
    particleVelocities_.reserve(kb::scene::kParticleEffectMaxCpuParticlesPerScene);
    particleAges_.reserve(kb::scene::kParticleEffectMaxCpuParticlesPerScene);
    particleLifetimes_.reserve(kb::scene::kParticleEffectMaxCpuParticlesPerScene);
    particleColors_.reserve(kb::scene::kParticleEffectMaxCpuParticlesPerScene);
    particleSizes_.reserve(kb::scene::kParticleEffectMaxCpuParticlesPerScene);
    particleEventDepths_.reserve(kb::scene::kParticleEffectMaxCpuParticlesPerScene);
    particlePrewarmGroups_.reserve(kb::scene::kParticleEffectMaxCpuParticlesPerScene);
    renderEmitterScratch_.resize(kb::particles::kParticleRenderSnapshotMaxEmitterRecords);
    renderParticleScratch_.resize(kb::scene::kParticleEffectMaxCpuParticlesPerScene);
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
    kb::scene::Scene&,
    std::uint64_t instanceId) noexcept {
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
    kb::scene::Scene&,
    std::uint64_t instanceId) noexcept {
    return Submit({ .type = CommandType::Restart, .instanceId = instanceId });
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
    renderRejectedByCapacity_.fill(0U);
    renderRejectedBySpawnBudget_.fill(0U);
    renderRejectedByEventBudget_.fill(0U);
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
    for (std::uint32_t denseIndex = 0U; denseIndex < denseInstanceCount_; ++denseIndex) {
        reloadRestarted_[denseIndex] = 0U;
    }
    RefreshCompiledEffects(scene);
    ProcessOwnerLifecycle(scene);
    stepTelemetry_ = {};
    renderRejectedByCapacity_.fill(0U);
    renderRejectedBySpawnBudget_.fill(0U);
    renderRejectedByEventBudget_.fill(0U);
    BeginEventStep();
    std::uint32_t remainingSpawnBudget = kb::scene::kParticleEffectMaxSpawnsPerStep;
    for (std::uint32_t denseIndex = 0U; denseIndex < denseInstanceCount_; ++denseIndex) {
        if (playbackStates_[denseIndex] == PlaybackState::Playing) {
            StepInstance(denseIndex, fixedDeltaSeconds, remainingSpawnBudget);
        }
    }
    AdvanceParticleAges(fixedDeltaSeconds);
    ExecuteForcesAndIntegrate(fixedDeltaSeconds);
    static_cast<void>(ProcessInternalEvents(remainingSpawnBudget));
    ProcessOwnerLifecycle(scene);
    return Result(kb::particles::ParticleRuntimeStatus::Success);
}

kb::particles::ParticleRuntimeQueryResult CpuParticleBackend::Query(
    const kb::scene::Scene&,
    std::uint64_t instanceId) const noexcept {
    const std::uint32_t denseIndex = ResolveDenseIndex(instanceId);
    if (denseIndex == kInvalidDenseIndex) {
        return { .status = kb::particles::ParticleRuntimeStatus::InvalidInstance };
    }
    return {
        .status = compiledEffects_[instanceRuntime_[denseIndex].compiledEffectIndex].invalidCandidateGeneration != 0U
            ? kb::particles::ParticleRuntimeStatus::StaleAfterInvalidReload
            : (reloadRestarted_[denseIndex] != 0U
                ? kb::particles::ParticleRuntimeStatus::Restarted
                : kb::particles::ParticleRuntimeStatus::Success),
        .state = playbackStates_[denseIndex] == PlaybackState::Playing,
        .assetId = effectAssetIds_[denseIndex],
        .materialAssetId = 0U,
        .liveParticleCount = LiveParticleCount(instanceId),
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
        particleEmitterIndices_[index] = particleEmitterIndices_[last];
        particlePositions_[index] = particlePositions_[last];
        particlePreviousPositions_[index] = particlePreviousPositions_[last];
        particleVelocities_[index] = particleVelocities_[last];
        particleAges_[index] = particleAges_[last];
        particleLifetimes_[index] = particleLifetimes_[last];
        particleColors_[index] = particleColors_[last];
        particleSizes_[index] = particleSizes_[last];
        particleEventDepths_[index] = particleEventDepths_[last];
        particlePrewarmGroups_[index] = particlePrewarmGroups_[last];
        particleInstanceIds_.pop_back();
        particleIds_.pop_back();
        particleEmitterIndices_.pop_back();
        particlePositions_.pop_back();
        particlePreviousPositions_.pop_back();
        particleVelocities_.pop_back();
        particleAges_.pop_back();
        particleLifetimes_.pop_back();
        particleColors_.pop_back();
        particleSizes_.pop_back();
        particleEventDepths_.pop_back();
        particlePrewarmGroups_.pop_back();
    }
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
    if (!asset.IsLoaded() || metadata == nullptr) return kInvalidDenseIndex;

    const ParticleCompileResult compiled = ParticleEffectCompiler::Compile(*asset, *metadata, manager.Registry());
    if (!compiled.Succeeded()) {
        failureStatus = std::any_of(compiled.diagnostics.begin(), compiled.diagnostics.end(), [](const auto& diagnostic) {
            return diagnostic.code == kb::scene::ParticleEffectDiagnosticCode::UnsupportedCapability;
        }) ? kb::particles::ParticleRuntimeStatus::UnsupportedOutput
           : kb::particles::ParticleRuntimeStatus::InvalidAsset;
        return kInvalidDenseIndex;
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
    const CompiledEmitter& emitter = effect.emitters[emitterIndex];
    const std::uint32_t instanceLive = LiveParticleCount(
        MakeInstanceId(denseToSlot_[denseIndex], slotGenerations_[denseToSlot_[denseIndex]]));
    const std::uint32_t instanceLimit = InstanceParticleLimit(denseIndex);
    if (count > emitter.maxParticles - runtime.liveParticles[emitterIndex] ||
        instanceLive >= instanceLimit || count > instanceLimit - instanceLive ||
        count > kb::scene::kParticleEffectMaxCpuParticlesPerScene - particleInstanceIds_.size()) {
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
    for (std::uint32_t index = 0U; index < count; ++index) {
        const float lifetime = emitter.lifetimeMin +
            (emitter.lifetimeMax - emitter.lifetimeMin) * NextRandom01(runtime);
        particleInstanceIds_.push_back(instanceId);
        particleIds_.push_back(nextParticleId_++);
        if (nextParticleId_ == 0U) nextParticleId_ = 1U;
        particleEmitterIndices_.push_back(emitterIndex);
        particlePositions_.push_back(eventPosition != nullptr
            ? *eventPosition + emitterOffset
            : ownerTransform.position + emitterOffset);
        particlePreviousPositions_.push_back(particlePositions_.back());
        particleVelocities_.push_back(TransformDirection(ownerTransform, SampleInitialVelocity(emitter, runtime)));
        particleAges_.push_back(0.0F);
        particleLifetimes_.push_back(lifetime);
        particleColors_.push_back(kb::math::Color{});
        particleSizes_.push_back(1.0F);
        particleEventDepths_.push_back(eventDepth);
        particlePrewarmGroups_.push_back(prewarmGroup);
        static_cast<void>(QueueInternalEvent({
            .instanceId = instanceId,
            .sourceEmitterIndex = emitterIndex,
            .trigger = kb::scene::ParticleEventTrigger::Birth,
            .depth = eventDepth,
            .prewarmGroup = prewarmGroup,
            .position = particlePositions_.back(),
        }));
        ++runtime.spawnOrdinal;
    }
    runtime.liveParticles[emitterIndex] += count;
    return true;
}

void CpuParticleBackend::SpawnRequested(
    std::uint32_t denseIndex,
    std::uint8_t emitterIndex,
    std::uint32_t count,
    std::uint32_t& remainingSpawnBudget,
    std::uint8_t prewarmGroup) noexcept {
    stepTelemetry_.requestedSpawns += count;
    const std::uint32_t withinBudget = std::min(count, remainingSpawnBudget);
    stepTelemetry_.rejectedByStepBudget += count - withinBudget;
    renderRejectedBySpawnBudget_[RenderGroupIndex(denseIndex, emitterIndex)] += count - withinBudget;
    remainingSpawnBudget -= withinBudget;
    if (withinBudget == 0U) return;
    InstanceRuntime& runtime = instanceRuntime_[denseIndex];
    const CompiledEmitter& emitter = compiledEffects_[runtime.compiledEffectIndex].effect->emitters[emitterIndex];
    const std::uint32_t emitterSpace = emitter.maxParticles - runtime.liveParticles[emitterIndex];
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
    if (accepted != 0U && SpawnExact(denseIndex, emitterIndex, accepted, 0U, prewarmGroup)) {
        stepTelemetry_.spawned += accepted;
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
    float localPrevious = previous;
    float localCurrent = previous + activeDelta;
    bool wrapped = false;
    if (effect.looping && effect.durationSeconds > 0.0F) {
        localPrevious = std::fmod(previous, effect.durationSeconds);
        localCurrent = localPrevious + activeDelta;
        if (localCurrent >= effect.durationSeconds) {
            localCurrent = std::fmod(localCurrent, effect.durationSeconds);
            wrapped = true;
        }
    }

    if (wrapped) runtime.nextBurst[emitterIndex] = 0U;
    if (emitter.mode == kb::scene::ParticleSpawnMode::Continuous) {
        const float sampleTime = wrapped ? localCurrent * 0.5F : (localPrevious + localCurrent) * 0.5F;
        runtime.emissionFractions[emitterIndex] +=
            std::max(0.0F, EvaluateRate(emitter, sampleTime)) * rateMultipliers_[denseIndex] * activeDelta;
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
        fireRange(localPrevious, effect.durationSeconds, false);
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
        particleEmitterIndices_[index] = particleEmitterIndices_[last];
        particlePositions_[index] = particlePositions_[last];
        particlePreviousPositions_[index] = particlePreviousPositions_[last];
        particleVelocities_[index] = particleVelocities_[last];
        particleAges_[index] = particleAges_[last];
        particleLifetimes_[index] = particleLifetimes_[last];
        particleColors_[index] = particleColors_[last];
        particleSizes_[index] = particleSizes_[last];
        particleEventDepths_[index] = particleEventDepths_[last];
        particlePrewarmGroups_[index] = particlePrewarmGroups_[last];
        particleInstanceIds_.pop_back();
        particleIds_.pop_back();
        particleEmitterIndices_.pop_back();
        particlePositions_.pop_back();
        particlePreviousPositions_.pop_back();
        particleVelocities_.pop_back();
        particleAges_.pop_back();
        particleLifetimes_.pop_back();
        particleColors_.pop_back();
        particleSizes_.pop_back();
        particleEventDepths_.pop_back();
        particlePrewarmGroups_.pop_back();
        ++stepTelemetry_.deaths;
    }
}

void CpuParticleBackend::ExecuteForcesAndIntegrate(
    float fixedDeltaSeconds,
    std::uint64_t onlyInstanceId,
    std::uint8_t onlyPrewarmGroup) noexcept {
    for (std::size_t index = 0U; index < particleInstanceIds_.size(); ++index) {
        if (onlyInstanceId != 0U && particleInstanceIds_[index] != onlyInstanceId) continue;
        if (onlyPrewarmGroup != UINT8_MAX && particlePrewarmGroups_[index] != onlyPrewarmGroup) continue;
        const std::uint32_t denseIndex = ResolveDenseIndex(particleInstanceIds_[index]);
        if (denseIndex == kInvalidDenseIndex ||
            (playbackStates_[denseIndex] != PlaybackState::Playing &&
             playbackStates_[denseIndex] != PlaybackState::Draining)) {
            continue;
        }
        const InstanceRuntime& runtime = instanceRuntime_[denseIndex];
        const CompiledEmitter& emitter =
            compiledEffects_[runtime.compiledEffectIndex].effect->emitters[particleEmitterIndices_[index]];
        kb::math::Vec3& velocity = particleVelocities_[index];
        kb::math::Color color{};
        float size = 1.0F;
        float alphaMultiplier = 1.0F;
        const float normalizedAge = kb::math::Clamp(
            particleAges_[index] / particleLifetimes_[index], 0.0F, 1.0F);
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
            case kb::scene::ParticleModuleType::ColorOverLife:
                color = EvaluateGradient(emitter.colorOverLife, normalizedAge);
                break;
            case kb::scene::ParticleModuleType::SizeOverLife:
                size = EvaluateCurve(emitter.sizeOverLife, normalizedAge);
                break;
            case kb::scene::ParticleModuleType::AlphaOverLife:
                alphaMultiplier = EvaluateCurve(emitter.alphaOverLife, normalizedAge);
                break;
            default:
                break;
            }
        }
        color.a *= alphaMultiplier;
        particleColors_[index] = color;
        particleSizes_[index] = size;
        particlePositions_[index] = particlePositions_[index] + velocity * fixedDeltaSeconds;
        for (std::uint8_t moduleIndex = 0U; moduleIndex < emitter.moduleCount; ++moduleIndex) {
            const CompiledEmitter::Module& module = emitter.modules[moduleIndex];
            if (!module.enabled || module.type != kb::scene::ParticleModuleType::CollisionPlane) continue;
            const kb::scene::ParticleCollisionPlaneModule& collision =
                std::get<kb::scene::ParticleCollisionPlaneModule>(module.payload);
            const float signedDistance = kb::math::Dot(collision.normal, particlePositions_[index]) -
                collision.distance;
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
    collisionEventsThisStep_.fill(0U);
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
    std::uint64_t fixedStepIndex,
    std::uint64_t revision) noexcept {
    renderGroupCounts_.fill(0U);
    for (std::size_t particleIndex = 0U; particleIndex < particleInstanceIds_.size(); ++particleIndex) {
        const std::uint32_t denseIndex = ResolveDenseIndex(particleInstanceIds_[particleIndex]);
        if (denseIndex == kInvalidDenseIndex) {
            return {kb::particles::ParticleRenderSnapshotStatus::InvalidSnapshot};
        }
        ++renderGroupCounts_[RenderGroupIndex(denseIndex, particleEmitterIndices_[particleIndex])];
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
                .assetGeneration = effectEntry.assetGeneration,
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
                .flipbookColumnsEncoded = static_cast<std::uint8_t>(emitter.flipbookColumns),
                .flipbookRowsEncoded = static_cast<std::uint8_t>(emitter.flipbookRows),
                .localBasisQuaternionSnorm = PackQuaternion(kb::math::Normalize(
                    ownerTransforms_[denseIndex].rotation * emitter.localRotation)),
                .pointSpriteDiameter = emitter.pointSpriteDiameter,
                .meshLodLevel = static_cast<std::int8_t>(std::clamp(
                    std::lround(emitter.meshLodBias), -128L, 127L)),
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
    if (particleOffset != static_cast<std::uint32_t>(particleInstanceIds_.size())) {
        return {kb::particles::ParticleRenderSnapshotStatus::InvalidSnapshot};
    }

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
            .rotationRadians = 0.0F,
            .velocity = particleVelocities_[particleIndex],
            .stretch = std::max(emitter.stretchMinimumLength, speed * emitter.stretchVelocityScale),
            .particleId = particleIds_[particleIndex],
            .packedColor = PackColor(particleColors_[particleIndex]),
            .frame = frame,
            .normalizedAgeUnorm = static_cast<std::uint16_t>(std::lround(
                kb::math::Clamp(particleLifetimes_[particleIndex] > 0.0F
                        ? particleAges_[particleIndex] / particleLifetimes_[particleIndex]
                        : 0.0F,
                    0.0F,
                    1.0F) * 65535.0F)),
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

    return kb::particles::ParticlePlayback::PublishRenderSnapshot(scene, *this, {
        .revision = revision,
        .fixedStepIndex = fixedStepIndex,
        .emitters = std::span<const kb::particles::ParticleRenderEmitterRecord>{
            renderEmitterScratch_.data(), emitterRecordCount},
        .particles = std::span<const kb::particles::ParticleRenderRecord>{
            renderParticleScratch_.data(), particleInstanceIds_.size()},
    });
}

kb::particles::ParticleRenderSnapshotResult CpuParticleBackend::PublishRenderTombstone(
    kb::scene::Scene& scene,
    std::uint64_t fixedStepIndex,
    std::uint64_t revision) noexcept {
    return kb::particles::ParticlePlayback::PublishRenderSnapshot(scene, *this, {
        .revision = revision,
        .fixedStepIndex = fixedStepIndex,
        .tombstone = true,
    });
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
    return static_cast<std::uint32_t>(std::count(particleInstanceIds_.begin(), particleInstanceIds_.end(), instanceId));
}

} // namespace kb::particle_plugin
