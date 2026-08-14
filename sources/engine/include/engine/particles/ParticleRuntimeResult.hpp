#pragma once

#include "engine/math/EngineMath.hpp"
#include "engine/scene/SceneEntity.hpp"

#include <cstdint>

namespace kb::particles {

enum class ParticleRuntimeStatus : std::uint8_t {
    Success,
    BackendUnavailable,
    InvalidAsset,
    InvalidOwner,
    InvalidInstance,
    InvalidParameter,
    InstanceLimitReached,
    UnsupportedOutput,
    InvalidRequest,
    BackendAlreadyRegistered,
    EventQueueFull,
    EventBudgetExceeded,
    ParticleCapacityReached,
    SpawnBudgetExceeded,
    Restarted,
    StaleAfterInvalidReload,
};

struct ParticleRuntimeResult {
    ParticleRuntimeStatus status = ParticleRuntimeStatus::BackendUnavailable;
    std::uint64_t instanceId = 0U;

    [[nodiscard]] constexpr bool Succeeded() const noexcept {
        return status == ParticleRuntimeStatus::Success;
    }
};

struct ParticleRuntimeQueryResult {
    ParticleRuntimeStatus status = ParticleRuntimeStatus::BackendUnavailable;
    bool state = false;
    std::uint64_t assetId = 0U;
    std::uint64_t materialAssetId = 0U;
    std::uint32_t liveParticleCount = 0U;

    [[nodiscard]] constexpr bool Succeeded() const noexcept {
        return status == ParticleRuntimeStatus::Success ||
            status == ParticleRuntimeStatus::Restarted ||
            status == ParticleRuntimeStatus::StaleAfterInvalidReload;
    }
};

struct ParticleRuntimeState {
    kb::math::Vec3 position{};
    kb::math::Vec3 velocity{};
    float age = 0.0F;
    float lifetime = 1.0F;
    kb::math::Color color{};
    float size = 1.0F;
};

struct PendingParticleRuntimeEvent {
    kb::scene::SceneEntity target{};
    std::uint64_t instanceId = 0U;
    std::uint64_t effectAssetId = 0U;
};

} // namespace kb::particles
