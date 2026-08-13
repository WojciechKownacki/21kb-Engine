#include "CpuParticleBackend.hpp"

#include "engine/assets/AssetId.hpp"
#include "engine/assets/AssetMetadata.hpp"
#include "engine/scene/ParticleEffectAssetIO.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneAssets.hpp"
#include "engine/scene/SceneEntities.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

namespace kb::particle_plugin {
namespace {

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
    parameters_.reserve(kb::scene::kParticleEffectMaxInstancesPerScene *
                        kb::scene::kParticleEffectMaxRuntimeParametersPerInstance);
    commands_.reserve(kb::scene::kParticleEffectMaxCommandsPerStep);
    events_.reserve(kb::scene::kParticleEffectMaxEventsPerStep);
    particleInstanceIds_.reserve(kb::scene::kParticleEffectMaxCpuParticlesPerScene);
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

    std::uint32_t slot = 0U;
    while (slot < slotToDense_.size() && slotToDense_[slot] != kInvalidDenseIndex) ++slot;
    if (slot == slotToDense_.size()) return Result(kb::particles::ParticleRuntimeStatus::InstanceLimitReached);

    const std::uint32_t denseIndex = denseInstanceCount_++;
    slotToDense_[slot] = denseIndex;
    denseToSlot_[denseIndex] = slot;
    effectAssetIds_[denseIndex] = effectAssetId;
    owners_[denseIndex] = owner;
    seeds_[denseIndex] = 0U;
    playbackStates_[denseIndex] = PlaybackState::Stopped;
    const std::uint64_t instanceId = MakeInstanceId(slot, slotGenerations_[slot]);
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
        RemoveDenseInstance(denseIndex);
        return Result(kb::particles::ParticleRuntimeStatus::Success, command.instanceId);
    case CommandType::Play:
        playbackStates_[denseIndex] = PlaybackState::Playing;
        return Result(kb::particles::ParticleRuntimeStatus::Success, command.instanceId);
    case CommandType::Pause:
        if (playbackStates_[denseIndex] != PlaybackState::Playing) {
            return Result(kb::particles::ParticleRuntimeStatus::InvalidRequest, command.instanceId);
        }
        playbackStates_[denseIndex] = PlaybackState::Paused;
        return Result(kb::particles::ParticleRuntimeStatus::Success, command.instanceId);
    case CommandType::Stop:
        playbackStates_[denseIndex] = PlaybackState::Stopped;
        RemoveParticles(command.instanceId);
        return Result(kb::particles::ParticleRuntimeStatus::Success, command.instanceId);
    case CommandType::Restart:
        RemoveParticles(command.instanceId);
        playbackStates_[denseIndex] = PlaybackState::Playing;
        return Result(kb::particles::ParticleRuntimeStatus::Success, command.instanceId);
    case CommandType::SetSeed:
        seeds_[denseIndex] = command.seed;
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
    if (ResolveDenseIndex(instanceId) == kInvalidDenseIndex) {
        return Result(kb::particles::ParticleRuntimeStatus::InvalidInstance, instanceId);
    }
    return Result(count == 0U ? kb::particles::ParticleRuntimeStatus::InvalidRequest
                              : kb::particles::ParticleRuntimeStatus::UnsupportedOutput,
        instanceId);
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
        particlePositions_[index] = particlePositions_[last];
        particleVelocities_[index] = particleVelocities_[last];
        particleAges_[index] = particleAges_[last];
        particleLifetimes_[index] = particleLifetimes_[last];
        particleInstanceIds_.pop_back();
        particlePositions_.pop_back();
        particleVelocities_.pop_back();
        particleAges_.pop_back();
        particleLifetimes_.pop_back();
    }
}

std::uint32_t CpuParticleBackend::LiveParticleCount(std::uint64_t instanceId) const noexcept {
    return static_cast<std::uint32_t>(std::count(particleInstanceIds_.begin(), particleInstanceIds_.end(), instanceId));
}

} // namespace kb::particle_plugin
