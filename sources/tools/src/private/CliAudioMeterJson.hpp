#pragma once

#include "engine/audio/AudioMetering.hpp"
#include "engine/core/JsonValue.hpp"

namespace kb::cli {
[[nodiscard]] inline kb::core::JsonValue AudioMeterJson(const kb::audio::AudioMeterReading& reading) {
    using kb::core::JsonValue;
    auto result = JsonValue::MakeObject();
    result.Set("bus", JsonValue::MakeString(reading.bus));
    result.Set("available", JsonValue::MakeBool(reading.available));
    result.Set("channels", JsonValue::MakeNumber(reading.channels));
    result.Set("block_frames", JsonValue::MakeNumber(reading.blockFrames));
    result.Set("processed_frames", JsonValue::MakeString(std::to_string(reading.processedFrames)));
    auto channels = JsonValue::MakeArray();
    for (std::size_t i = 0U; i < reading.channels && i < reading.channel.size(); ++i) {
        const auto& from = reading.channel[i];
        auto channel = JsonValue::MakeObject();
        channel.Set("peak", JsonValue::MakeNumber(from.peak)); channel.Set("rms", JsonValue::MakeNumber(from.rms));
        channel.Set("held_peak", JsonValue::MakeNumber(from.heldPeak));
        channel.Set("clipped_samples", JsonValue::MakeString(std::to_string(from.clippedSamples)));
        channel.Set("non_finite_samples", JsonValue::MakeString(std::to_string(from.nonFiniteSamples)));
        channels.Append(std::move(channel));
    }
    result.Set("channel", std::move(channels));
    return result;
}

[[nodiscard]] inline kb::core::JsonValue AudioMetersJson(const kb::audio::AudioMeterReport& report) {
    using kb::core::JsonValue;
    auto result = JsonValue::MakeObject();
    result.Set("supported", JsonValue::MakeBool(report.supported));
    result.Set("initialized", JsonValue::MakeBool(report.initialized));
    result.Set("capture_enabled", JsonValue::MakeBool(report.captureEnabled));
    result.Set("mixer_asset_id", JsonValue::MakeString(std::to_string(report.mixerAssetId)));
    result.Set("sample_rate", JsonValue::MakeNumber(report.sampleRate));
    result.Set("master", AudioMeterJson(report.master));
    auto buses = JsonValue::MakeArray();
    for (const auto& bus : report.buses) buses.Append(AudioMeterJson(bus));
    result.Set("buses", std::move(buses));
    return result;
}
} // namespace kb::cli
