#include "CpuParticleBackend.hpp"

#include "engine/assets/AssetId.hpp"
#include "engine/assets/AssetManager.hpp"
#include "engine/assets/AssetMetadata.hpp"
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

} // namespace

void CpuParticleBackend::Warmup() {
    if (warmedUp_) return;
    slotToDense_.fill(kInvalidDenseIndex);
    slotGenerations_.fill(1U);
    denseToSlot_.resize(kb::scene::kParticleEffectMaxInstancesPerScene);
    effectAssetIds_.resize(kb::scene::kParticleEffectMaxInstancesPerScene);
    owners_.resize(kb::scene::kParticleEffectMaxInstancesPerScene);
    seeds_.resize(kb::scene::kParticleEffectMaxInstancesPerScene);
    playbackStates_.resize(kb::scene::kParticleEffectMaxInstancesPerScene);
    instanceRuntime_.resize(kb::scene::kParticleEffectMaxInstancesPerScene);
    compiledEffects_.resize(kb::scene::kParticleEffectMaxInstancesPerScene);
    parameters_.reserve(kb::scene::kParticleEffectMaxInstancesPerScene *
                        kb::scene::kParticleEffectMaxRuntimeParametersPerInstance);
    commands_.reserve(kb::scene::kParticleEffectMaxCommandsPerStep);
    events_.reserve(kb::scene::kParticleEffectMaxEventsPerStep);
    particleInstanceIds_.reserve(kb::scene::kParticleEffectMaxCpuParticlesPerScene);
    particleEmitterIndices_.reserve(kb::scene::kParticleEffectMaxCpuParticlesPerScene);
    particlePositions_.reserve(kb::scene::kParticleEffectMaxCpuParticlesPerScene);
    particleVelocities_.reserve(kb::scene::kParticleEffectMaxCpuParticlesPerScene);
    particleAges_.reserve(kb::scene::kParticleEffectMaxCpuParticlesPerScene);
    particleLifetimes_.reserve(kb::scene::kParticleEffectMaxCpuParticlesPerScene);
    warmedUp_ = true;
}

bool CpuParticleBackend::IsWarmedUp() const noexcept { return warmedUp_; }
std::size_t CpuParticleBackend::LiveInstanceCount() const noexcept { return denseInstanceCount_; }
std::size_t CpuParticleBackend::ParticleCapacity() const noexcept { return particlePositions_.capacity(); }
std::size_t CpuParticleBackend::BufferedCommandCount() const noexcept { return commands_.size(); }
std::size_t CpuParticleBackend::BufferedEventCount() const noexcept { return events_.size(); }
CpuParticleBackend::StepTelemetry CpuParticleBackend::LastStepTelemetry() const noexcept { return stepTelemetry_; }

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
    seeds_[denseIndex] = compiledEffects_[compiledEffectIndex].determinismSeed;
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
        std::erase_if(parameters_, [&](const ParameterEntry& entry) { return entry.instanceId == command.instanceId; });
        ReleaseCompiledEffect(instanceRuntime_[denseIndex].compiledEffectIndex);
        RemoveDenseInstance(denseIndex);
        return Result(kb::particles::ParticleRuntimeStatus::Success, command.instanceId);
    case CommandType::Play:
        if (playbackStates_[denseIndex] == PlaybackState::Stopped) {
            ResetInstance(denseIndex);
            playbackStates_[denseIndex] = PlaybackState::Playing;
            PrewarmInstance(denseIndex);
            return Result(kb::particles::ParticleRuntimeStatus::Success, command.instanceId);
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
        ResetInstance(denseIndex);
        playbackStates_[denseIndex] = PlaybackState::Playing;
        PrewarmInstance(denseIndex);
        return Result(kb::particles::ParticleRuntimeStatus::Success, command.instanceId);
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
    stepTelemetry_.requestedSpawns = count;
    if (count > kb::scene::kParticleEffectMaxSpawnsPerStep) {
        stepTelemetry_.rejectedByStepBudget = count;
        return Result(kb::particles::ParticleRuntimeStatus::SpawnBudgetExceeded, instanceId);
    }
    if (!SpawnExact(denseIndex, 0U, count)) {
        stepTelemetry_.rejectedByCapacity = count;
        return Result(kb::particles::ParticleRuntimeStatus::ParticleCapacityReached, instanceId);
    }
    stepTelemetry_.spawned = count;
    return Result(kb::particles::ParticleRuntimeStatus::Success, instanceId);
}

kb::particles::ParticleRuntimeResult CpuParticleBackend::Step(
    kb::scene::Scene&,
    float fixedDeltaSeconds) noexcept {
    if (!warmedUp_ || !std::isfinite(fixedDeltaSeconds) ||
        std::abs(fixedDeltaSeconds - kb::scene::kSceneRuntimeDefaultFixedDeltaSeconds) > 0.0000001F) {
        return Result(kb::particles::ParticleRuntimeStatus::InvalidRequest);
    }
    stepTelemetry_ = {};
    std::uint32_t remainingSpawnBudget = kb::scene::kParticleEffectMaxSpawnsPerStep;
    for (std::uint32_t denseIndex = 0U; denseIndex < denseInstanceCount_; ++denseIndex) {
        if (playbackStates_[denseIndex] == PlaybackState::Playing) {
            StepInstance(denseIndex, fixedDeltaSeconds, remainingSpawnBudget);
        }
    }
    AdvanceParticleAges(fixedDeltaSeconds);
    ExecuteForcesAndIntegrate(fixedDeltaSeconds);
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
        .status = kb::particles::ParticleRuntimeStatus::Success,
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
        seeds_[denseIndex] = seeds_[last];
        playbackStates_[denseIndex] = playbackStates_[last];
        instanceRuntime_[denseIndex] = instanceRuntime_[last];
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
        particleEmitterIndices_[index] = particleEmitterIndices_[last];
        particlePositions_[index] = particlePositions_[last];
        particleVelocities_[index] = particleVelocities_[last];
        particleAges_[index] = particleAges_[last];
        particleLifetimes_[index] = particleLifetimes_[last];
        particleInstanceIds_.pop_back();
        particleEmitterIndices_.pop_back();
        particlePositions_.pop_back();
        particleVelocities_.pop_back();
        particleAges_.pop_back();
        particleLifetimes_.pop_back();
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
        CompiledEffect& cached = compiledEffects_[index];
        if (cached.referenceCount != 0U && cached.assetId == effectAssetId &&
            cached.assetGeneration == generation) {
            ++cached.referenceCount;
            return index;
        }
    }

    const auto handle = manager.Load<kb::scene::ParticleEffectAsset>(assetId);
    if (!handle.IsLoaded() || handle->formatVersion != kb::scene::kParticleEffectFormatVersion ||
        !kb::scene::ParticleEffectAssetValidator::ValidateStructure(*handle).Succeeded()) {
        return kInvalidDenseIndex;
    }
    if (handle->backendPolicy == kb::scene::ParticleBackendPolicy::GpuVisualRequired ||
        !handle->eventBindings.empty()) {
        failureStatus = kb::particles::ParticleRuntimeStatus::UnsupportedOutput;
        return kInvalidDenseIndex;
    }
    for (const kb::scene::ParticleEmitterAsset& emitter : handle->emitters) {
        if (std::any_of(emitter.modules.begin(), emitter.modules.end(), [](const kb::scene::ParticleModuleAsset& module) {
                return module.type != kb::scene::ParticleModuleType::InitialVelocity &&
                       module.type != kb::scene::ParticleModuleType::Gravity &&
                       module.type != kb::scene::ParticleModuleType::Wind &&
                       module.type != kb::scene::ParticleModuleType::Drag;
            })) {
            failureStatus = kb::particles::ParticleRuntimeStatus::UnsupportedOutput;
            return kInvalidDenseIndex;
        }
    }

    std::uint32_t freeIndex = kInvalidDenseIndex;
    for (std::uint32_t index = 0U; index < compiledEffects_.size(); ++index) {
        if (compiledEffects_[index].referenceCount == 0U) {
            freeIndex = index;
            break;
        }
    }
    if (freeIndex == kInvalidDenseIndex) return kInvalidDenseIndex;

    CompiledEffect compiled{};
    compiled.assetId = effectAssetId;
    compiled.assetGeneration = generation;
    compiled.determinismSeed = handle->determinismSeed;
    compiled.durationSeconds = handle->durationSeconds;
    compiled.looping = handle->looping;
    compiled.emitterCount = static_cast<std::uint8_t>(handle->emitters.size());
    compiled.referenceCount = 1U;
    for (std::size_t emitterIndex = 0U; emitterIndex < handle->emitters.size(); ++emitterIndex) {
        const kb::scene::ParticleEmitterAsset& source = handle->emitters[emitterIndex];
        CompiledEmitter& destination = compiled.emitters[emitterIndex];
        destination.enabled = source.enabled;
        destination.mode = source.spawn.mode;
        destination.maxParticles = source.maxParticles;
        destination.localPosition = source.localPosition;
        destination.initialVelocity = {
            .direction = kb::math::Normalize(source.spawn.direction),
            .speedMin = source.spawn.speedMin,
            .speedMax = source.spawn.speedMax,
            .randomization = source.spawn.randomization,
            .spreadDegrees = source.spawn.spreadDegrees,
        };
        destination.lifetimeMin = source.spawn.lifetimeMin;
        destination.lifetimeMax = source.spawn.lifetimeMax;
        destination.prewarmSeconds = source.spawn.prewarmSeconds;
        destination.rateKeyCount = static_cast<std::uint8_t>(source.spawn.rateOverTime.keyframes.size());
        destination.burstCount = static_cast<std::uint8_t>(source.spawn.bursts.size());
        for (std::size_t keyIndex = 0U; keyIndex < source.spawn.rateOverTime.keyframes.size(); ++keyIndex) {
            const kb::math::CurveKeyframe& key = source.spawn.rateOverTime.keyframes[keyIndex];
            destination.rateKeys[keyIndex] = { .time = key.time, .value = key.value, .easing = key.easing };
        }
        std::copy(source.spawn.bursts.begin(), source.spawn.bursts.end(), destination.bursts.begin());
        destination.moduleCount = static_cast<std::uint8_t>(source.modules.size());
        for (std::size_t moduleIndex = 0U; moduleIndex < source.modules.size(); ++moduleIndex) {
            const kb::scene::ParticleModuleAsset& sourceModule = source.modules[moduleIndex];
            CompiledEmitter::Module& destinationModule = destination.modules[moduleIndex];
            destinationModule.type = sourceModule.type;
            destinationModule.enabled = sourceModule.enabled;
            switch (sourceModule.type) {
            case kb::scene::ParticleModuleType::InitialVelocity:
                destinationModule.payload = std::get<kb::scene::ParticleInitialVelocityModule>(sourceModule.payload);
                if (sourceModule.enabled) {
                    destination.initialVelocity =
                        std::get<kb::scene::ParticleInitialVelocityModule>(sourceModule.payload);
                    destination.initialVelocity.direction = kb::math::Normalize(destination.initialVelocity.direction);
                }
                break;
            case kb::scene::ParticleModuleType::Gravity:
                destinationModule.payload = std::get<kb::scene::ParticleGravityModule>(sourceModule.payload);
                break;
            case kb::scene::ParticleModuleType::Wind:
                destinationModule.payload = std::get<kb::scene::ParticleWindModule>(sourceModule.payload);
                break;
            case kb::scene::ParticleModuleType::Drag:
                destinationModule.payload = std::get<kb::scene::ParticleDragModule>(sourceModule.payload);
                break;
            default:
                break;
            }
        }
    }
    compiledEffects_[freeIndex] = compiled;
    return freeIndex;
}

void CpuParticleBackend::ReleaseCompiledEffect(std::uint32_t index) noexcept {
    if (index < compiledEffects_.size() && compiledEffects_[index].referenceCount != 0U) {
        --compiledEffects_[index].referenceCount;
    }
}

void CpuParticleBackend::ResetInstance(std::uint32_t denseIndex) noexcept {
    InstanceRuntime& runtime = instanceRuntime_[denseIndex];
    const std::uint32_t compiledIndex = runtime.compiledEffectIndex;
    runtime = {};
    runtime.compiledEffectIndex = compiledIndex;
    const std::uint64_t instanceId = MakeInstanceId(denseToSlot_[denseIndex], slotGenerations_[denseToSlot_[denseIndex]]);
    runtime.randomState = seeds_[denseIndex] ^ (instanceId * 0x9E3779B97F4A7C15ULL);
}

void CpuParticleBackend::PrewarmInstance(std::uint32_t denseIndex) noexcept {
    const CompiledEffect& effect = compiledEffects_[instanceRuntime_[denseIndex].compiledEffectIndex];
    const std::uint64_t instanceId =
        MakeInstanceId(denseToSlot_[denseIndex], slotGenerations_[denseToSlot_[denseIndex]]);
    for (std::uint8_t emitterIndex = 0U; emitterIndex < effect.emitterCount; ++emitterIndex) {
        const std::uint32_t steps = static_cast<std::uint32_t>(std::floor(
            static_cast<double>(effect.emitters[emitterIndex].prewarmSeconds) /
            static_cast<double>(kb::scene::kSceneRuntimeDefaultFixedDeltaSeconds) + 0.5));
        for (std::uint32_t step = 0U; step < steps; ++step) {
            stepTelemetry_ = {};
            std::uint32_t budget = kb::scene::kParticleEffectMaxSpawnsPerStep;
            StepEmitter(denseIndex, emitterIndex, kb::scene::kSceneRuntimeDefaultFixedDeltaSeconds, budget);
            AdvanceParticleAges(kb::scene::kSceneRuntimeDefaultFixedDeltaSeconds, instanceId, emitterIndex);
            ExecuteForcesAndIntegrate(
                kb::scene::kSceneRuntimeDefaultFixedDeltaSeconds, instanceId, emitterIndex);
        }
    }
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

bool CpuParticleBackend::SpawnExact(std::uint32_t denseIndex, std::uint8_t emitterIndex, std::uint32_t count) noexcept {
    InstanceRuntime& runtime = instanceRuntime_[denseIndex];
    const CompiledEffect& effect = compiledEffects_[runtime.compiledEffectIndex];
    if (emitterIndex >= effect.emitterCount) return false;
    const CompiledEmitter& emitter = effect.emitters[emitterIndex];
    if (count > emitter.maxParticles - runtime.liveParticles[emitterIndex] ||
        count > kb::scene::kParticleEffectMaxCpuParticlesPerScene - particleInstanceIds_.size()) {
        return false;
    }
    const std::uint64_t instanceId = MakeInstanceId(denseToSlot_[denseIndex], slotGenerations_[denseToSlot_[denseIndex]]);
    for (std::uint32_t index = 0U; index < count; ++index) {
        const float lifetime = emitter.lifetimeMin +
            (emitter.lifetimeMax - emitter.lifetimeMin) * NextRandom01(runtime);
        particleInstanceIds_.push_back(instanceId);
        particleEmitterIndices_.push_back(emitterIndex);
        particlePositions_.push_back(emitter.localPosition);
        particleVelocities_.push_back(SampleInitialVelocity(emitter, runtime));
        particleAges_.push_back(0.0F);
        particleLifetimes_.push_back(lifetime);
        ++runtime.spawnOrdinal;
    }
    runtime.liveParticles[emitterIndex] += count;
    return true;
}

void CpuParticleBackend::SpawnRequested(
    std::uint32_t denseIndex,
    std::uint8_t emitterIndex,
    std::uint32_t count,
    std::uint32_t& remainingSpawnBudget) noexcept {
    stepTelemetry_.requestedSpawns += count;
    const std::uint32_t withinBudget = std::min(count, remainingSpawnBudget);
    stepTelemetry_.rejectedByStepBudget += count - withinBudget;
    remainingSpawnBudget -= withinBudget;
    if (withinBudget == 0U) return;
    InstanceRuntime& runtime = instanceRuntime_[denseIndex];
    const CompiledEmitter& emitter = compiledEffects_[runtime.compiledEffectIndex].emitters[emitterIndex];
    const std::uint32_t emitterSpace = emitter.maxParticles - runtime.liveParticles[emitterIndex];
    const std::uint32_t sceneSpace = static_cast<std::uint32_t>(
        kb::scene::kParticleEffectMaxCpuParticlesPerScene - particleInstanceIds_.size());
    const std::uint32_t accepted = std::min({withinBudget, emitterSpace, sceneSpace});
    stepTelemetry_.rejectedByCapacity += withinBudget - accepted;
    if (accepted != 0U && SpawnExact(denseIndex, emitterIndex, accepted)) {
        stepTelemetry_.spawned += accepted;
    }
}

void CpuParticleBackend::StepInstance(
    std::uint32_t denseIndex,
    float fixedDeltaSeconds,
    std::uint32_t& remainingSpawnBudget) noexcept {
    InstanceRuntime& runtime = instanceRuntime_[denseIndex];
    const CompiledEffect& effect = compiledEffects_[runtime.compiledEffectIndex];
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
    std::uint32_t& remainingSpawnBudget) noexcept {
    InstanceRuntime& runtime = instanceRuntime_[denseIndex];
    const CompiledEffect& effect = compiledEffects_[runtime.compiledEffectIndex];
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
            std::max(0.0F, EvaluateRate(emitter, sampleTime)) * activeDelta;
        const std::uint32_t continuous = static_cast<std::uint32_t>(runtime.emissionFractions[emitterIndex]);
        runtime.emissionFractions[emitterIndex] -= static_cast<float>(continuous);
        SpawnRequested(denseIndex, emitterIndex, continuous, remainingSpawnBudget);
    }
    auto fireRange = [&](float begin, float end, bool includeZero) noexcept {
        while (runtime.nextBurst[emitterIndex] < emitter.burstCount) {
            const kb::scene::ParticleBurstAsset& burst = emitter.bursts[runtime.nextBurst[emitterIndex]];
            if (burst.timeSeconds > end) break;
            ++runtime.nextBurst[emitterIndex];
            if ((includeZero ? burst.timeSeconds >= begin : burst.timeSeconds > begin)) {
                SpawnRequested(denseIndex, emitterIndex, burst.count, remainingSpawnBudget);
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
    std::uint8_t onlyEmitterIndex) noexcept {
    std::size_t index = 0U;
    while (index < particleAges_.size()) {
        if (onlyInstanceId != 0U && particleInstanceIds_[index] != onlyInstanceId) {
            ++index;
            continue;
        }
        if (onlyEmitterIndex != UINT8_MAX && particleEmitterIndices_[index] != onlyEmitterIndex) {
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
        particleEmitterIndices_[index] = particleEmitterIndices_[last];
        particlePositions_[index] = particlePositions_[last];
        particleVelocities_[index] = particleVelocities_[last];
        particleAges_[index] = particleAges_[last];
        particleLifetimes_[index] = particleLifetimes_[last];
        particleInstanceIds_.pop_back();
        particleEmitterIndices_.pop_back();
        particlePositions_.pop_back();
        particleVelocities_.pop_back();
        particleAges_.pop_back();
        particleLifetimes_.pop_back();
        ++stepTelemetry_.deaths;
    }
}

void CpuParticleBackend::ExecuteForcesAndIntegrate(
    float fixedDeltaSeconds,
    std::uint64_t onlyInstanceId,
    std::uint8_t onlyEmitterIndex) noexcept {
    for (std::size_t index = 0U; index < particleInstanceIds_.size(); ++index) {
        if (onlyInstanceId != 0U && particleInstanceIds_[index] != onlyInstanceId) continue;
        if (onlyEmitterIndex != UINT8_MAX && particleEmitterIndices_[index] != onlyEmitterIndex) continue;
        const std::uint32_t denseIndex = ResolveDenseIndex(particleInstanceIds_[index]);
        if (denseIndex == kInvalidDenseIndex ||
            (playbackStates_[denseIndex] != PlaybackState::Playing &&
             playbackStates_[denseIndex] != PlaybackState::Draining)) {
            continue;
        }
        const InstanceRuntime& runtime = instanceRuntime_[denseIndex];
        const CompiledEmitter& emitter =
            compiledEffects_[runtime.compiledEffectIndex].emitters[particleEmitterIndices_[index]];
        kb::math::Vec3& velocity = particleVelocities_[index];
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
            default:
                break;
            }
        }
        particlePositions_[index] = particlePositions_[index] + velocity * fixedDeltaSeconds;
    }
}

std::uint32_t CpuParticleBackend::LiveParticleCount(std::uint64_t instanceId) const noexcept {
    return static_cast<std::uint32_t>(std::count(particleInstanceIds_.begin(), particleInstanceIds_.end(), instanceId));
}

} // namespace kb::particle_plugin
