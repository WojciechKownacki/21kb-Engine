#pragma once

#include "engine/audio/AudioMixerAsset.hpp"

#include <span>
#include <string>
#include <string_view>

namespace kb::audio {

[[nodiscard]] std::span<const std::string_view> AudioDspFieldNames(AudioDspKind kind) noexcept;
[[nodiscard]] std::string AudioDspFieldValue(const AudioDspParameters& parameters, std::string_view field);
// Empty error means success. Invalid edits leave the authored value unchanged.
[[nodiscard]] std::string SetAudioDspField(AudioDspParameters& parameters, std::string_view field, std::string_view value);
[[nodiscard]] std::string AddAudioMixerEffect(AudioMixerAsset& asset, std::string_view bus, std::string_view id, AudioDspKind kind);
[[nodiscard]] std::string RemoveAudioMixerEffect(AudioMixerAsset& asset, std::string_view bus, std::string_view id);
[[nodiscard]] std::string MoveAudioMixerEffect(AudioMixerAsset& asset, std::string_view bus, std::string_view id, std::size_t index);
[[nodiscard]] std::string SetAudioMixerEffectField(AudioMixerAsset& asset, std::string_view bus, std::string_view id,
    std::string_view field, std::string_view value);

} // namespace kb::audio
