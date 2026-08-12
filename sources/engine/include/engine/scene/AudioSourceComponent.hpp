#pragma once

#include "engine/audio/AudioRoutingContract.hpp"
#include "engine/audio/AudioSettings.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cmath>
#include <string_view>

namespace kb::scene {

struct AudioSourceComponent {
    // LIB-147: outputBus names an AudioMixerBus of the scene's active AudioMixer asset
    // (SceneAudioMixerAccess); empty routes to the implicit master output (the pre-mixer
    // behavior every source authored before LIB-147 keeps). Fixed-capacity char storage
    // because scene components must stay trivially copyable (the archetype storage moves
    // them bytewise) - the exact TagsComponent convention; use the
    // AudioSourceOutputBus/SetAudioSourceOutputBus helpers below. Empty selects master;
    // a non-empty unknown bus is unavailable and never silently reaches master.
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
    if (component.outputBusLength > AudioSourceComponent::MaxOutputBusBytes
        || component.outputBus[component.outputBusLength] != '\0') {
        return std::string_view{ "\0", 1U };
    }
    for (std::uint32_t index = 0U; index < component.outputBusLength; ++index) {
        if (component.outputBus[index] == '\0') {
            return std::string_view{ "\0", 1U };
        }
    }
    return std::string_view{ component.outputBus.data(), component.outputBusLength };
}

inline bool IsAudioSourceOutputBusValid(const AudioSourceComponent& component) noexcept {
    const std::string_view bus = AudioSourceOutputBus(component);
    return bus.empty() || (bus.front() != '\0' && kb::audio::IsAudioMixerNameTokenValid(bus));
}

[[nodiscard]] inline bool IsAudioSourceComponentPersistable(const AudioSourceComponent& component) noexcept {
    const bool attenuationModelValid = [model = component.attenuationModel]() noexcept {
        switch (model) {
        case kb::audio::AudioAttenuationModel::None:
        case kb::audio::AudioAttenuationModel::Inverse:
        case kb::audio::AudioAttenuationModel::Linear:
        case kb::audio::AudioAttenuationModel::Exponential:
            return true;
        }
        return false;
    }();
    return std::isfinite(component.volume) && component.volume >= 0.0F
        && std::isfinite(component.pitch) && component.pitch >= 0.01F
        && std::isfinite(component.pan) && component.pan >= -1.0F && component.pan <= 1.0F
        && std::isfinite(component.spatialBlend) && component.spatialBlend >= 0.0F && component.spatialBlend <= 1.0F
        && std::isfinite(component.minDistance) && component.minDistance >= 0.01F
        && std::isfinite(component.maxDistance) && component.maxDistance >= component.minDistance
        && std::isfinite(component.rolloff) && component.rolloff >= 0.0F
        && std::isfinite(component.dopplerFactor) && component.dopplerFactor >= 0.0F
        && attenuationModelValid
        && IsAudioSourceOutputBusValid(component);
}

[[nodiscard]] inline bool SetAudioSourceOutputBus(AudioSourceComponent& component, std::string_view busName) noexcept {
    if (busName.size() > AudioSourceComponent::MaxOutputBusBytes
        || (!busName.empty() && !kb::audio::IsAudioMixerNameTokenValid(busName))) {
        return false;
    }

    const std::uint32_t length = static_cast<std::uint32_t>(busName.size());
    std::fill(component.outputBus.begin(), component.outputBus.end(), '\0');
    for (std::uint32_t index = 0U; index < length; ++index) {
        component.outputBus[index] = busName[index];
    }
    component.outputBusLength = length;
    return true;
}

} // namespace kb::scene
