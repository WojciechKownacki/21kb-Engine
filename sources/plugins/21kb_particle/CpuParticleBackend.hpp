#pragma once

#include "engine/particles/IParticleSimulationBackend.hpp"
#include "engine/scene/ParticleEffectAssetSchema.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

namespace kb::particle_plugin {

class CpuParticleBackend final : public kb::particles::IParticleSimulationBackend {
public:
    CpuParticleBackend() = default;

    void Warmup();
    [[nodiscard]] bool IsWarmedUp() const noexcept;
    [[nodiscard]] std::size_t LiveInstanceCount() const noexcept;
    [[nodiscard]] std::size_t ParticleCapacity() const noexcept;
    [[nodiscard]] static constexpr std::size_t CommandCapacity() noexcept {
        return kb::scene::kParticleEffectMaxCommandsPerStep;
    }
    [[nodiscard]] static constexpr std::size_t EventCapacity() noexcept {
        return kb::scene::kParticleEffectMaxEventsPerStep;
    }
    [[nodiscard]] std::size_t BufferedCommandCount() const noexcept;
    [[nodiscard]] std::size_t BufferedEventCount() const noexcept;

    [[nodiscard]] kb::particles::ParticleRuntimeResult Create(
        kb::scene::Scene& scene,
        std::uint64_t effectAssetId,
        kb::scene::SceneEntity owner) override;
    [[nodiscard]] kb::particles::ParticleRuntimeResult Release(
        kb::scene::Scene& scene,
        std::uint64_t instanceId) noexcept override;
    [[nodiscard]] kb::particles::ParticleRuntimeResult Play(
        kb::scene::Scene& scene,
        std::uint64_t instanceId) noexcept override;
    [[nodiscard]] kb::particles::ParticleRuntimeResult Pause(
        kb::scene::Scene& scene,
        std::uint64_t instanceId) noexcept override;
    [[nodiscard]] kb::particles::ParticleRuntimeResult Stop(
        kb::scene::Scene& scene,
        std::uint64_t instanceId) noexcept override;
    [[nodiscard]] kb::particles::ParticleRuntimeResult Restart(
        kb::scene::Scene& scene,
        std::uint64_t instanceId) noexcept override;
    [[nodiscard]] kb::particles::ParticleRuntimeResult SetSeed(
        kb::scene::Scene& scene,
        std::uint64_t instanceId,
        std::uint64_t seed) noexcept override;
    [[nodiscard]] kb::particles::ParticleRuntimeResult SetParameterScalar(
        kb::scene::Scene& scene,
        std::uint64_t instanceId,
        std::string_view name,
        float value) noexcept override;
    [[nodiscard]] kb::particles::ParticleRuntimeResult ClearParameter(
        kb::scene::Scene& scene,
        std::uint64_t instanceId,
        std::string_view name) noexcept override;
    [[nodiscard]] kb::particles::ParticleRuntimeResult Emit(
        kb::scene::Scene& scene,
        std::uint64_t instanceId,
        std::uint32_t count) override;
    [[nodiscard]] kb::particles::ParticleRuntimeQueryResult Query(
        const kb::scene::Scene& scene,
        std::uint64_t instanceId) const noexcept override;
    [[nodiscard]] std::size_t CopyLiveInstanceIds(
        const kb::scene::Scene& scene,
        std::span<std::uint64_t> output) const noexcept override;
    [[nodiscard]] std::size_t CopyLiveParticleStates(
        const kb::scene::Scene& scene,
        std::uint64_t instanceId,
        std::span<kb::particles::ParticleRuntimeState> output) const noexcept override;

private:
    enum class PlaybackState : std::uint8_t { Stopped, Playing, Paused };
    enum class CommandType : std::uint8_t { Release, Play, Pause, Stop, Restart, SetSeed, SetParameter, ClearParameter };

    struct RuntimeParameter {
        std::array<char, kb::scene::kParticleEffectMaxRuntimeParameterNameBytes> name{};
        std::uint16_t nameLength = 0U;
        float value = 0.0F;
    };

    struct ParameterEntry {
        std::uint64_t instanceId = 0U;
        RuntimeParameter parameter{};
    };

    struct Command {
        CommandType type = CommandType::Play;
        std::uint64_t instanceId = 0U;
        std::uint64_t seed = 0U;
        RuntimeParameter parameter{};
    };

    static constexpr std::uint32_t kInvalidDenseIndex = UINT32_MAX;

    [[nodiscard]] static std::uint64_t MakeInstanceId(std::uint32_t slot, std::uint32_t generation) noexcept;
    [[nodiscard]] std::uint32_t ResolveDenseIndex(std::uint64_t instanceId) const noexcept;
    [[nodiscard]] kb::particles::ParticleRuntimeResult Submit(Command command) noexcept;
    [[nodiscard]] kb::particles::ParticleRuntimeResult Execute(const Command& command) noexcept;
    void RemoveDenseInstance(std::uint32_t denseIndex) noexcept;
    void RemoveParticles(std::uint64_t instanceId) noexcept;
    [[nodiscard]] std::uint32_t LiveParticleCount(std::uint64_t instanceId) const noexcept;

    bool warmedUp_ = false;
    std::uint32_t denseInstanceCount_ = 0U;
    std::array<std::uint32_t, kb::scene::kParticleEffectMaxInstancesPerScene> slotGenerations_{};
    std::array<std::uint32_t, kb::scene::kParticleEffectMaxInstancesPerScene> slotToDense_{};
    std::vector<std::uint32_t> denseToSlot_;
    std::vector<std::uint64_t> effectAssetIds_;
    std::vector<kb::scene::SceneEntity> owners_;
    std::vector<std::uint64_t> seeds_;
    std::vector<PlaybackState> playbackStates_;
    std::vector<ParameterEntry> parameters_;

    std::vector<Command> commands_;
    std::vector<kb::particles::PendingParticleRuntimeEvent> events_;

    std::vector<std::uint64_t> particleInstanceIds_;
    std::vector<kb::math::Vec3> particlePositions_;
    std::vector<kb::math::Vec3> particleVelocities_;
    std::vector<float> particleAges_;
    std::vector<float> particleLifetimes_;
};

} // namespace kb::particle_plugin
