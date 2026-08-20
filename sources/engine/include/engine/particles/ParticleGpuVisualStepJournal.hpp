#pragma once

#include "engine/scene/ParticleEffectAssetSchema.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace kb::particles {

enum class ParticleGpuVisualStepJournalStatus : std::uint8_t {
    Success,
    StaleStep,
    GpuCatchupOverflow,
};

struct ParticleGpuVisualStep {
    std::uint64_t revision = 0U;
    std::uint64_t backendEpoch = 0U;
    std::uint64_t fixedStepIndex = 0U;
};

// The journal is core-owned so all renderer viewports consume the same fixed-step
// sequence. It intentionally retains no renderer handles or particle payloads.
class ParticleGpuVisualStepJournal final {
public:
    [[nodiscard]] ParticleGpuVisualStepJournalStatus Publish(
        std::uint64_t revision,
        std::uint64_t backendEpoch,
        std::uint64_t fixedStepIndex) noexcept {
        if (revision == 0U || backendEpoch == 0U || fixedStepIndex == 0U ||
            (count_ != 0U && fixedStepIndex <= steps_[count_ - 1U].fixedStepIndex)) {
            return ParticleGpuVisualStepJournalStatus::StaleStep;
        }
        if (count_ == steps_.size()) {
            overflowed_ = true;
            return ParticleGpuVisualStepJournalStatus::GpuCatchupOverflow;
        }
        steps_[count_++] = {
            .revision = revision,
            .backendEpoch = backendEpoch,
            .fixedStepIndex = fixedStepIndex,
        };
        return ParticleGpuVisualStepJournalStatus::Success;
    }

    [[nodiscard]] ParticleGpuVisualStepJournalStatus AcknowledgeThrough(
        std::uint64_t fixedStepIndex) noexcept {
        if (overflowed_) return ParticleGpuVisualStepJournalStatus::GpuCatchupOverflow;
        if (count_ == 0U || fixedStepIndex < steps_[0].fixedStepIndex ||
            fixedStepIndex > steps_[count_ - 1U].fixedStepIndex) {
            return ParticleGpuVisualStepJournalStatus::StaleStep;
        }
        std::size_t consumed = 0U;
        while (consumed < count_ && steps_[consumed].fixedStepIndex <= fixedStepIndex) {
            ++consumed;
        }
        for (std::size_t index = consumed; index < count_; ++index) {
            steps_[index - consumed] = steps_[index];
        }
        count_ -= consumed;
        return ParticleGpuVisualStepJournalStatus::Success;
    }

    [[nodiscard]] std::span<const ParticleGpuVisualStep> Pending() const noexcept {
        return {steps_.data(), count_};
    }

    [[nodiscard]] bool Overflowed() const noexcept { return overflowed_; }

    void Clear() noexcept {
        count_ = 0U;
        overflowed_ = false;
    }

private:
    std::array<ParticleGpuVisualStep, kb::scene::kParticleEffectRetainedGpuSteps> steps_{};
    std::size_t count_ = 0U;
    bool overflowed_ = false;
};

} // namespace kb::particles
