#pragma once

#include <cstdint>

namespace kb::render {

enum class SceneGpuDrivenFeatureState : std::uint8_t {
    Disabled,
    CpuValidationOnly,
    ComputeCulling,
    IndirectDrawSubmit,
    MeshletSubmit,
};

enum class SceneGpuDrivenCounterSource : std::uint8_t {
    None,
    CpuCandidates,
    GpuDispatchCounters,
    DebugReadback,
};

enum class SceneGpuDrivenFallbackReason : std::uint8_t {
    None,
    FeatureNotRequested,
    ComputeUnsupported,
    IndirectDrawUnsupported,
    MeshletSubmitUnsupported,
    RuntimeGpuDispatchUnavailable,
};

struct SceneGpuDrivenFeatureRequest {
    bool gpuCullingRequested = false;
    bool indirectDrawRequested = false;
    bool meshletSubmitRequested = false;

    [[nodiscard]] constexpr bool HasAnyRequest() const noexcept {
        return gpuCullingRequested || indirectDrawRequested || meshletSubmitRequested;
    }
};

struct SceneGpuDrivenFeatureSupport {
    bool computeCullingSupported = false;
    bool indirectDrawSupported = false;
    bool meshletSubmitSupported = false;
    bool runtimeGpuDispatchSupported = false;
};

struct SceneGpuDrivenFeatureDecision {
    SceneGpuDrivenFeatureState state = SceneGpuDrivenFeatureState::Disabled;
    SceneGpuDrivenCounterSource counterSource = SceneGpuDrivenCounterSource::None;
    SceneGpuDrivenFallbackReason fallbackReason = SceneGpuDrivenFallbackReason::None;

    [[nodiscard]] constexpr bool UsesFallback() const noexcept {
        return fallbackReason != SceneGpuDrivenFallbackReason::None &&
            fallbackReason != SceneGpuDrivenFallbackReason::FeatureNotRequested;
    }
};

class SceneGpuDrivenFeatureClassifier {
public:
    SceneGpuDrivenFeatureClassifier() = delete;

    [[nodiscard]] static constexpr SceneGpuDrivenFeatureDecision Decide(
        SceneGpuDrivenFeatureRequest request,
        SceneGpuDrivenFeatureSupport support) noexcept {
        const SceneGpuDrivenFeatureState state = Resolve(request, support);
        return SceneGpuDrivenFeatureDecision{
            .state = state,
            .counterSource = CounterSourceForState(state),
            .fallbackReason = FallbackReasonFor(request, support, state),
        };
    }

    [[nodiscard]] static constexpr SceneGpuDrivenFeatureState Resolve(
        SceneGpuDrivenFeatureRequest request,
        SceneGpuDrivenFeatureSupport support) noexcept {
        if (!request.HasAnyRequest()) {
            return SceneGpuDrivenFeatureState::Disabled;
        }
        if (!support.computeCullingSupported) {
            return SceneGpuDrivenFeatureState::CpuValidationOnly;
        }
        if (!support.runtimeGpuDispatchSupported) {
            return SceneGpuDrivenFeatureState::CpuValidationOnly;
        }
        if (request.indirectDrawRequested && !support.indirectDrawSupported) {
            return SceneGpuDrivenFeatureState::ComputeCulling;
        }
        if (request.meshletSubmitRequested && support.computeCullingSupported &&
            support.indirectDrawSupported && support.meshletSubmitSupported) {
            return SceneGpuDrivenFeatureState::MeshletSubmit;
        }
        if (request.indirectDrawRequested && support.computeCullingSupported && support.indirectDrawSupported) {
            return SceneGpuDrivenFeatureState::IndirectDrawSubmit;
        }
        if ((request.gpuCullingRequested || request.indirectDrawRequested || request.meshletSubmitRequested) &&
            support.computeCullingSupported) {
            return SceneGpuDrivenFeatureState::ComputeCulling;
        }
        return SceneGpuDrivenFeatureState::CpuValidationOnly;
    }

    [[nodiscard]] static constexpr SceneGpuDrivenCounterSource CounterSourceForState(
        SceneGpuDrivenFeatureState state) noexcept {
        switch (state) {
        case SceneGpuDrivenFeatureState::Disabled:
            return SceneGpuDrivenCounterSource::None;
        case SceneGpuDrivenFeatureState::CpuValidationOnly:
            return SceneGpuDrivenCounterSource::CpuCandidates;
        case SceneGpuDrivenFeatureState::ComputeCulling:
        case SceneGpuDrivenFeatureState::IndirectDrawSubmit:
        case SceneGpuDrivenFeatureState::MeshletSubmit:
            return SceneGpuDrivenCounterSource::GpuDispatchCounters;
        }
        return SceneGpuDrivenCounterSource::None;
    }

private:
    [[nodiscard]] static constexpr SceneGpuDrivenFallbackReason FallbackReasonFor(
        SceneGpuDrivenFeatureRequest request,
        SceneGpuDrivenFeatureSupport support,
        SceneGpuDrivenFeatureState state) noexcept {
        if (!request.HasAnyRequest()) {
            return SceneGpuDrivenFallbackReason::FeatureNotRequested;
        }
        if (!support.computeCullingSupported) {
            return SceneGpuDrivenFallbackReason::ComputeUnsupported;
        }
        if (request.indirectDrawRequested && !support.indirectDrawSupported) {
            return SceneGpuDrivenFallbackReason::IndirectDrawUnsupported;
        }
        if (!support.runtimeGpuDispatchSupported) {
            return SceneGpuDrivenFallbackReason::RuntimeGpuDispatchUnavailable;
        }
        if (request.meshletSubmitRequested &&
            static_cast<std::uint8_t>(state) < static_cast<std::uint8_t>(SceneGpuDrivenFeatureState::MeshletSubmit)) {
            return support.indirectDrawSupported
                ? SceneGpuDrivenFallbackReason::MeshletSubmitUnsupported
                : SceneGpuDrivenFallbackReason::IndirectDrawUnsupported;
        }
        if (request.indirectDrawRequested &&
            static_cast<std::uint8_t>(state) < static_cast<std::uint8_t>(SceneGpuDrivenFeatureState::IndirectDrawSubmit)) {
            return SceneGpuDrivenFallbackReason::IndirectDrawUnsupported;
        }
        return SceneGpuDrivenFallbackReason::None;
    }
};

[[nodiscard]] constexpr std::uint8_t SceneGpuDrivenFeatureStateRank(SceneGpuDrivenFeatureState state) noexcept {
    return static_cast<std::uint8_t>(state);
}

[[nodiscard]] constexpr SceneGpuDrivenFeatureState MaxSceneGpuDrivenFeatureState(
    SceneGpuDrivenFeatureState lhs,
    SceneGpuDrivenFeatureState rhs) noexcept {
    return SceneGpuDrivenFeatureStateRank(rhs) > SceneGpuDrivenFeatureStateRank(lhs) ? rhs : lhs;
}

[[nodiscard]] constexpr std::uint8_t SceneGpuDrivenCounterSourceRank(SceneGpuDrivenCounterSource source) noexcept {
    switch (source) {
    case SceneGpuDrivenCounterSource::None:
        return 0U;
    case SceneGpuDrivenCounterSource::CpuCandidates:
        return 1U;
    case SceneGpuDrivenCounterSource::GpuDispatchCounters:
        return 2U;
    case SceneGpuDrivenCounterSource::DebugReadback:
        return 3U;
    }
    return 0U;
}

[[nodiscard]] constexpr SceneGpuDrivenCounterSource MaxSceneGpuDrivenCounterSource(
    SceneGpuDrivenCounterSource lhs,
    SceneGpuDrivenCounterSource rhs) noexcept {
    return SceneGpuDrivenCounterSourceRank(rhs) > SceneGpuDrivenCounterSourceRank(lhs) ? rhs : lhs;
}

[[nodiscard]] constexpr std::uint8_t SceneGpuDrivenFallbackReasonRank(SceneGpuDrivenFallbackReason reason) noexcept {
    switch (reason) {
    case SceneGpuDrivenFallbackReason::None:
    case SceneGpuDrivenFallbackReason::FeatureNotRequested:
        return 0U;
    case SceneGpuDrivenFallbackReason::ComputeUnsupported:
        return 4U;
    case SceneGpuDrivenFallbackReason::IndirectDrawUnsupported:
        return 3U;
    case SceneGpuDrivenFallbackReason::MeshletSubmitUnsupported:
        return 2U;
    case SceneGpuDrivenFallbackReason::RuntimeGpuDispatchUnavailable:
        return 1U;
    }
    return 0U;
}

[[nodiscard]] constexpr SceneGpuDrivenFallbackReason MaxSceneGpuDrivenFallbackReason(
    SceneGpuDrivenFallbackReason lhs,
    SceneGpuDrivenFallbackReason rhs) noexcept {
    return SceneGpuDrivenFallbackReasonRank(rhs) > SceneGpuDrivenFallbackReasonRank(lhs) ? rhs : lhs;
}

} // namespace kb::render
