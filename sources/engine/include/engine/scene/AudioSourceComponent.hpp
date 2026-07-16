#pragma once

#include "engine/audio/AudioSettings.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace kb::scene {

struct AudioSourceComponent {
    // LIB-147: outputBus names an AudioMixerBus of the scene's active AudioMixer asset
    // (SceneAudioMixerAccess); empty routes to the implicit master output (the pre-mixer
    // behavior every source authored before LIB-147 keeps). Fixed-capacity char storage
    // because scene components must stay trivially copyable (the archetype storage moves
    // them bytewise) - the exact TagsComponent convention; use the
    // AudioSourceOutputBus/SetAudioSourceOutputBus helpers below. An unknown bus name
    // honestly falls back to master at the backend, mirroring how an unresolvable
    // clipAssetId already behaves.
    static constexpr std::uint32_t MaxOutputBusBytes = 63U;

    std::uint64_t clipAssetId = 0;
    float volume = 1.0F;
    float pitch = 1.0F;
    bool loop = false;
    bool spatial = true;
    bool autoplay = false;
    bool enabled = true;
    bool mute = false;
    float pan = 0.0F;
    float spatialBlend = 1.0F;
    kb::audio::AudioAttenuationModel attenuationModel = kb::audio::AudioAttenuationModel::Inverse;
    float minDistance = 1.0F;
    float maxDistance = 500.0F;
    float rolloff = 1.0F;
    float dopplerFactor = 1.0F;
    std::array<char, MaxOutputBusBytes + 1U> outputBus{};
    std::uint32_t outputBusLength = 0U;
};

inline std::string_view AudioSourceOutputBus(const AudioSourceComponent& component) noexcept {
    return std::string_view{ component.outputBus.data(), component.outputBusLength };
}

inline void SetAudioSourceOutputBus(AudioSourceComponent& component, std::string_view busName) noexcept {
    const std::uint32_t length = static_cast<std::uint32_t>(std::min<std::size_t>(busName.size(), AudioSourceComponent::MaxOutputBusBytes));
    std::fill(component.outputBus.begin(), component.outputBus.end(), '\0');
    for (std::uint32_t index = 0U; index < length; ++index) {
        component.outputBus[index] = busName[index];
    }
    component.outputBusLength = length;
}

} // namespace kb::scene
