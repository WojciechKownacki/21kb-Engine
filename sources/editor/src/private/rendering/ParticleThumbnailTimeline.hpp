#pragma once

#include "engine/scene/ParticleEffectAsset.hpp"
#include "engine/scene/SceneRuntime.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace kb::editor {

struct ParticleThumbnailTimelinePlan {
    float durationSeconds = kb::scene::kSceneRuntimeDefaultFixedDeltaSeconds;
    std::uint32_t simulationSteps = 1U;
    std::uint32_t frameCount = 2U;
    bool looping = true;
    bool usesBoundedPreviewWindow = false;
};

class ParticleThumbnailTimeline final {
public:
    static constexpr float kFramesPerSecond = 24.0F;
    static constexpr std::uint32_t kMinimumFrameCount = 2U;
    static constexpr std::uint32_t kMaximumFrameCount = 240U;
    static constexpr float kUnboundedPreviewSeconds = 5.0F;
    static constexpr float kMaximumPreviewSeconds = 10.0F;
    static constexpr float kPosterTargetSeconds = 0.2F;

    [[nodiscard]] static ParticleThumbnailTimelinePlan Plan(
        float authoredDurationSeconds,
        bool looping) noexcept {
        const float fixedDelta = kb::scene::kSceneRuntimeDefaultFixedDeltaSeconds;
        const bool finitePositive = std::isfinite(authoredDurationSeconds) &&
            authoredDurationSeconds > 0.0F;
        const float requestedDuration = finitePositive
            ? authoredDurationSeconds
            : kUnboundedPreviewSeconds;
        const float duration = std::min(
            requestedDuration, kMaximumPreviewSeconds);
        const auto simulationSteps = static_cast<std::uint32_t>(std::max(
            1.0,
            std::ceil(static_cast<double>(duration) /
                static_cast<double>(fixedDelta))));
        const auto frameCount = static_cast<std::uint32_t>(std::clamp(
            std::ceil(static_cast<double>(duration) *
                static_cast<double>(kFramesPerSecond)),
            static_cast<double>(kMinimumFrameCount),
            static_cast<double>(kMaximumFrameCount)));
        return ParticleThumbnailTimelinePlan{
            .durationSeconds = duration,
            .simulationSteps = simulationSteps,
            .frameCount = frameCount,
            .looping = looping,
            .usesBoundedPreviewWindow = !finitePositive ||
                requestedDuration > kMaximumPreviewSeconds,
        };
    }

    [[nodiscard]] static ParticleThumbnailTimelinePlan Plan(
        const kb::scene::ParticleEffectAsset& asset) noexcept {
        float playbackDuration = asset.durationSeconds;
        float maximumEmitterLifetime = 0.0F;
        if (std::isfinite(playbackDuration) &&
            playbackDuration > 0.0F) {
            for (const kb::scene::ParticleEmitterAsset& emitter :
                 asset.emitters) {
                if (!emitter.enabled) continue;
                const float lifetime = std::max(
                    emitter.spawn.lifetimeMin,
                    emitter.spawn.lifetimeMax);
                if (!std::isfinite(lifetime) || lifetime <= 0.0F) continue;
                maximumEmitterLifetime = std::max(
                    maximumEmitterLifetime, lifetime);
                if (asset.looping) continue;
                if (emitter.spawn.mode ==
                    kb::scene::ParticleSpawnMode::Continuous) {
                    // Emission remains active through the authored duration;
                    // include the last particle's natural drain interval.
                    playbackDuration = std::max(
                        playbackDuration,
                        asset.durationSeconds + lifetime);
                    continue;
                }
                for (const kb::scene::ParticleBurstAsset& burst :
                     emitter.spawn.bursts) {
                    if (burst.count == 0U ||
                        !std::isfinite(burst.timeSeconds)) {
                        continue;
                    }
                    playbackDuration = std::max(
                        playbackDuration,
                        std::max(0.0F, burst.timeSeconds) + lifetime);
                }
            }
        }
        if (!asset.looping && maximumEmitterLifetime > 0.0F) {
            std::uint32_t maximumEventDepth = 0U;
            for (const kb::scene::ParticleEventBindingAsset& binding :
                 asset.eventBindings) {
                maximumEventDepth = std::max(
                    maximumEventDepth, binding.maxDepth);
            }
            for (const kb::scene::ParticleEmitterAsset& emitter :
                 asset.emitters) {
                if (!emitter.enabled) continue;
                for (const kb::scene::ParticleModuleAsset& module :
                     emitter.modules) {
                    if (!module.enabled || module.type !=
                        kb::scene::ParticleModuleType::SubEmitter) {
                        continue;
                    }
                    if (const auto* subEmitter = std::get_if<
                            kb::scene::ParticleSubEmitterModule>(
                            &module.payload)) {
                        maximumEventDepth = std::max(
                            maximumEventDepth, subEmitter->maxDepth);
                    }
                }
            }
            playbackDuration += maximumEmitterLifetime *
                static_cast<float>(maximumEventDepth);
        }
        return Plan(playbackDuration, asset.looping);
    }

    [[nodiscard]] static std::uint32_t CaptureStep(
        const ParticleThumbnailTimelinePlan& plan,
        std::uint32_t frameIndex) noexcept {
        if (plan.frameCount == 0U || plan.simulationSteps == 0U) return 1U;
        const std::uint32_t clampedFrame = std::min(
            frameIndex, plan.frameCount - 1U);
        if (plan.looping) {
            // Sample [0, duration) so the final frame does not duplicate the
            // wrapped first frame. The runtime needs one fixed step to create
            // and publish the auto-play instance, hence the lower bound of 1.
            return std::max(
                1U,
                static_cast<std::uint32_t>(
                    (static_cast<std::uint64_t>(clampedFrame) *
                        plan.simulationSteps) /
                    plan.frameCount));
        }
        // A one-shot includes its authored end/drain frame.
        return std::max(
            1U,
            static_cast<std::uint32_t>(
                (static_cast<std::uint64_t>(clampedFrame + 1U) *
                    plan.simulationSteps) /
                plan.frameCount));
    }

    [[nodiscard]] static std::uint32_t PosterFrame(
        const ParticleThumbnailTimelinePlan& plan) noexcept {
        if (plan.frameCount <= 1U || !(plan.durationSeconds > 0.0F)) {
            return 0U;
        }
        const float targetSeconds = std::min(
            kPosterTargetSeconds, plan.durationSeconds * 0.35F);
        return std::min(
            plan.frameCount - 1U,
            static_cast<std::uint32_t>(std::floor(
                static_cast<double>(targetSeconds) *
                static_cast<double>(kFramesPerSecond))));
    }

    [[nodiscard]] static std::uint32_t FrameAtSeconds(
        const ParticleThumbnailTimelinePlan& plan,
        double elapsedSeconds) noexcept {
        if (plan.frameCount <= 1U || !(plan.durationSeconds > 0.0F) ||
            !std::isfinite(elapsedSeconds)) {
            return 0U;
        }
        double local = std::fmod(
            std::max(0.0, elapsedSeconds),
            static_cast<double>(plan.durationSeconds));
        if (local < 0.0) local += plan.durationSeconds;
        return std::min(
            plan.frameCount - 1U,
            static_cast<std::uint32_t>(
                local / static_cast<double>(plan.durationSeconds) *
                static_cast<double>(plan.frameCount)));
    }
};

} // namespace kb::editor
