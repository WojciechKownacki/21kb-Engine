#pragma once

#include <cmath>
#include <cstdint>
#include <initializer_list>
#include <optional>
#include <string>
#include <string_view>

namespace kb::audio {

enum class AudioDspKind : std::uint8_t { LowPass, HighPass, PeakingEqualizer, Delay, Reverb, Compressor, Limiter };

// Authored settings are independent of a device format. The provider also checks
// its active sample rate, channel count, quantized coefficients and state budget.
struct AudioDspParameters {
    AudioDspKind kind = AudioDspKind::LowPass;
    double cutoffHz = 1000.0;
    std::uint32_t order = 2U;
    bool bypass = false;
    double gainDb = 0.0;
    double quality = 1.0;
    double delayMs = 250.0;
    float feedback = 0.0F;
    float wet = 0.5F;
    float dry = 1.0F;
    float roomSize = 0.5F;
    float damping = 0.25F;
    float width = 1.0F;
    double thresholdDb = -12.0;
    double ratio = 4.0;
    double attackMs = 10.0;
    double releaseMs = 100.0;
    double makeupDb = 0.0;
    double ceilingDb = -1.0;

    [[nodiscard]] friend bool operator==(const AudioDspParameters&, const AudioDspParameters&) noexcept = default;
};

struct AudioDspEffect {
    std::string id;
    AudioDspParameters parameters;

    [[nodiscard]] friend bool operator==(const AudioDspEffect&, const AudioDspEffect&) noexcept = default;
};

[[nodiscard]] inline constexpr std::string_view AudioDspKindName(AudioDspKind kind) noexcept {
    switch (kind) {
    case AudioDspKind::LowPass: return "lowpass";
    case AudioDspKind::HighPass: return "highpass";
    case AudioDspKind::PeakingEqualizer: return "equalizer";
    case AudioDspKind::Delay: return "delay";
    case AudioDspKind::Reverb: return "reverb";
    case AudioDspKind::Compressor: return "compressor";
    case AudioDspKind::Limiter: return "limiter";
    }
    return {};
}

[[nodiscard]] inline constexpr std::optional<AudioDspKind> ParseAudioDspKind(std::string_view name) noexcept {
    for (const auto kind : {AudioDspKind::LowPass, AudioDspKind::HighPass, AudioDspKind::PeakingEqualizer,
            AudioDspKind::Delay, AudioDspKind::Reverb, AudioDspKind::Compressor, AudioDspKind::Limiter}) {
        if (AudioDspKindName(kind) == name) return kind;
    }
    return std::nullopt;
}

// An empty result means valid. All retained settings, including bypassed ones,
// are checked; unused fields keep defaults so no authored parameter is discarded.
[[nodiscard]] inline std::string_view AudioDspParameterError(const AudioDspParameters& p) noexcept {
    if (AudioDspKindName(p.kind).empty()) return "unknown DSP effect kind";
    if (!std::isfinite(p.cutoffHz) || p.cutoffHz < 1.0 || p.cutoffHz > 94080.0) return "cutoffHz must be finite and between 1 and 94080";
    if (p.order < 1U || p.order > 8U) return "filter order must be between 1 and 8";
    if (!std::isfinite(p.gainDb) || p.gainDb < -24.0 || p.gainDb > 24.0) return "gainDb must be finite and between -24 and 24";
    if (!std::isfinite(p.quality) || p.quality < 0.1 || p.quality > 16.0) return "quality must be finite and between 0.1 and 16";
    if (!std::isfinite(p.delayMs) || p.delayMs <= 0.0 || p.delayMs > 2000.0) return "delayMs must be finite, positive and at most 2000";
    if (!std::isfinite(p.feedback) || p.feedback < 0.0F || p.feedback > 0.95F) return "feedback must be finite and between 0 and 0.95";
    const auto normalized = [](float value) { return std::isfinite(value) && value >= 0.0F && value <= 1.0F; };
    if (!normalized(p.wet) || !normalized(p.dry)) return "wet and dry must be finite gains between 0 and 1";
    if (!normalized(p.roomSize) || !normalized(p.damping) || !normalized(p.width)) return "roomSize, damping and width must be finite and between 0 and 1";
    if (!std::isfinite(p.thresholdDb) || p.thresholdDb < -60.0 || p.thresholdDb > 0.0) return "thresholdDb must be finite and between -60 and 0";
    if (!std::isfinite(p.ratio) || p.ratio < 1.0 || p.ratio > 20.0) return "ratio must be finite and between 1 and 20";
    if (!std::isfinite(p.attackMs) || p.attackMs < 0.0 || p.attackMs > 200.0) return "attackMs must be finite and between 0 and 200";
    if (!std::isfinite(p.releaseMs) || p.releaseMs < 1.0 || p.releaseMs > 5000.0) return "releaseMs must be finite and between 1 and 5000";
    if (!std::isfinite(p.makeupDb) || p.makeupDb < 0.0 || p.makeupDb > 24.0) return "makeupDb must be finite and between 0 and 24";
    if (!std::isfinite(p.ceilingDb) || p.ceilingDb < -24.0 || p.ceilingDb > 0.0) return "ceilingDb must be finite and between -24 and 0";
    if (p.kind == AudioDspKind::PeakingEqualizer) {
        if (p.order != 2U) return "equalizer requires order 2";
    } else if (p.gainDb != 0.0 || p.quality != 1.0) return "gainDb and quality apply only to equalizer";
    if (p.kind != AudioDspKind::LowPass && p.kind != AudioDspKind::HighPass && p.kind != AudioDspKind::PeakingEqualizer
        && (p.cutoffHz != 1000.0 || p.order != 2U)) return "cutoffHz and order apply only to filters and equalizer";
    if (p.kind != AudioDspKind::Delay && (p.delayMs != 250.0 || p.feedback != 0.0F)) return "delayMs and feedback apply only to delay";
    if (p.kind != AudioDspKind::Delay && p.kind != AudioDspKind::Reverb && (p.wet != 0.5F || p.dry != 1.0F)) return "wet and dry apply only to delay and reverb";
    if (p.kind != AudioDspKind::Reverb && (p.roomSize != 0.5F || p.damping != 0.25F || p.width != 1.0F)) return "roomSize, damping and width apply only to reverb";
    if (p.kind != AudioDspKind::Compressor && (p.thresholdDb != -12.0 || p.ratio != 4.0 || p.attackMs != 10.0 || p.makeupDb != 0.0)) return "thresholdDb, ratio, attackMs and makeupDb apply only to compressor";
    if (p.kind != AudioDspKind::Compressor && p.kind != AudioDspKind::Limiter && p.releaseMs != 100.0) return "releaseMs applies only to compressor and limiter";
    if (p.kind != AudioDspKind::Limiter && p.ceilingDb != -1.0) return "ceilingDb applies only to limiter";
    return {};
}

} // namespace kb::audio
