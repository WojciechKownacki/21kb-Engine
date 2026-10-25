#pragma once

#include "engine/audio/AudioMixerEffectEditing.hpp"
#include "engine/audio/AudioDspRuntimeReport.hpp"
#include "engine/core/JsonValue.hpp"

#include <stdexcept>
#include <utility>

namespace kb::cli {

[[nodiscard]] inline kb::core::JsonValue AudioEffectJson(const kb::audio::AudioDspEffect& effect) {
    using kb::core::JsonValue;
    auto output = JsonValue::MakeObject();
    output.Set("id", JsonValue::MakeString(effect.id));
    output.Set("kind", JsonValue::MakeString(std::string{kb::audio::AudioDspKindName(effect.parameters.kind)}));
    auto parameters = JsonValue::MakeObject();
    for (const auto field : kb::audio::AudioDspFieldNames(effect.parameters.kind)) {
        JsonValue value; std::string error;
        if (!JsonValue::Parse(kb::audio::AudioDspFieldValue(effect.parameters, field), value, error))
            throw std::logic_error{"validated DSP parameter could not be serialized"};
        parameters.Set(std::string{field}, std::move(value));
    }
    output.Set("parameters", std::move(parameters));
    return output;
}

[[nodiscard]] inline kb::core::JsonValue AudioRuntimeJson(const kb::audio::AudioDspRuntimeReport& report) {
    using kb::core::JsonValue;
    auto output = JsonValue::MakeObject();
    output.Set("schema", JsonValue::MakeString("21kb.audio-dsp/v1"));
    output.Set("backend_available", JsonValue::MakeBool(report.backendAvailable));
    output.Set("offline", JsonValue::MakeBool(report.offline));
    output.Set("sample_rate", JsonValue::MakeNumber(report.sampleRate));
    output.Set("channels", JsonValue::MakeNumber(report.channels));
    output.Set("rendered_frames", JsonValue::MakeString(std::to_string(report.renderedFrames)));
    output.Set("mixer_asset_id", JsonValue::MakeString(std::to_string(report.mixerAssetId)));
    auto buses = JsonValue::MakeArray();
    for (const auto& bus : report.buses) {
        auto row = JsonValue::MakeObject();
        row.Set("bus", JsonValue::MakeString(bus.bus));
        row.Set("initialized", JsonValue::MakeBool(bus.initialized));
        row.Set("settings_current", JsonValue::MakeBool(bus.settingsCurrent));
        row.Set("active_effects", JsonValue::MakeNumber(static_cast<double>(bus.activeEffects)));
        row.Set("error", JsonValue::MakeString(bus.error));
        auto effects = JsonValue::MakeArray();
        for (const auto& effect : bus.appliedEffects) effects.Append(AudioEffectJson(effect));
        row.Set("applied_effects", std::move(effects)); buses.Append(std::move(row));
    }
    output.Set("buses", std::move(buses));
    return output;
}

} // namespace kb::cli
