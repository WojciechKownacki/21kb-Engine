#include "inspection/InspectorAudioTextBuilder.hpp"

#include "inspection/InspectorComponentLabelFormatter.hpp"

#include <cstdio>
#include <string_view>

namespace kb::editor {

void InspectorAudioSourceTextBuilder::Append(std::string& text, const kb::scene::AudioSourceComponent& audioSource) const {
    const std::string_view outputBus = kb::scene::AudioSourceOutputBus(audioSource);
    char component[512]{};
    std::snprintf(
        component,
        sizeof(component),
        "\n\nAudio Source\nClip: %llu\nVolume: %.2f\nPitch: %.2f\nLoop: %s\nAutoplay: %s\nEnabled: %s\nSpatial: %s\nAttenuation: %s\nRange: %.2f - %.2f\nOutput Bus: %.*s",
        static_cast<unsigned long long>(audioSource.clipAssetId),
        audioSource.volume,
        audioSource.pitch,
        audioSource.loop ? "true" : "false",
        audioSource.autoplay ? "true" : "false",
        audioSource.enabled ? "true" : "false",
        audioSource.spatial ? "true" : "false",
        InspectorComponentLabelFormatter::AudioAttenuationModelName(audioSource.attenuationModel),
        audioSource.minDistance,
        audioSource.maxDistance,
        outputBus.empty() ? 8 : static_cast<int>(outputBus.size()),
        outputBus.empty() ? "(master)" : outputBus.data());
    text += component;
}

void InspectorAudioListenerTextBuilder::Append(std::string& text, const kb::scene::AudioListenerComponent& audioListener) const {
    char component[160]{};
    std::snprintf(
        component,
        sizeof(component),
        "\n\nAudio Listener\nPrimary: %s\nEnabled: %s",
        audioListener.primary ? "true" : "false",
        audioListener.enabled ? "true" : "false");
    text += component;
}

} // namespace kb::editor
