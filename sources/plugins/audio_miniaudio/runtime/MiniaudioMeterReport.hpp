#pragma once

#include "runtime/MiniaudioMeterNode.hpp"
#include "engine/audio/AudioMetering.hpp"

#include <string_view>

namespace kb::audio_miniaudio {
[[nodiscard]] inline kb::audio::AudioMeterReading ReadAudioMeter(
    const MiniaudioMeterAccumulator& meter, std::string_view bus = {}) {
    kb::audio::AudioMeterReading result{.bus = std::string{bus}};
    MiniaudioMeterSnapshot snapshot;
    if (!meter.TryRead(snapshot)) return result;
    result.available = true; result.channels = snapshot.channels;
    result.blockFrames = snapshot.blockFrames; result.processedFrames = snapshot.processedFrames;
    for (std::size_t i = 0U; i < result.channel.size(); ++i) {
        const auto& from = snapshot.channel[i];
        result.channel[i] = {.peak = from.peak, .rms = from.rms, .heldPeak = from.heldPeak,
            .clippedSamples = from.clippedSamples, .nonFiniteSamples = from.nonFiniteSamples};
    }
    return result;
}
} // namespace kb::audio_miniaudio
