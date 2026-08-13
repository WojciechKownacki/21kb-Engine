#include "inspection/InspectorAudioTextBuilder.hpp"

#include "inspection/InspectorComponentLabelFormatter.hpp"

#include <cstdio>
#include <string_view>

namespace kb::editor {

void InspectorAudioSourceTextBuilder::Append(std::string& text, const kb::scene::AudioSourceComponent& audioSource) const {
    const std::string_view outputBus = kb::scene::AudioSourceOutputBus(audioSource);
    const bool outputBusValid = kb::scene::IsAudioSourceOutputBusValid(audioSource);
    const std::string_view outputBusDisplay = !outputBusValid
        ? std::string_view{ "(invalid)" }
        : outputBus.empty() ? std::string_view{ "(master)" } : outputBus;
    char component[768]{};
    std::snprintf(
        component,
        sizeof(component),
        "\n\nAudio Source\nClip: %llu\nVolume: %.2f\nPitch: %.2f\nLoop: %s\nAutoplay: %s\nEnabled: %s\nMute: %s\nSpatial: %s\nPan: %.2f\nSpatial Blend: %.2f\nAttenuation: %s\nMin Distance: %.2f\nMax Distance: %.2f\nRolloff: %.2f\nDoppler Factor: %.2f\nOutput Bus: %.*s",
        static_cast<unsigned long long>(audioSource.clipAssetId),
        audioSource.volume,
        audioSource.pitch,
        audioSource.loop ? "true" : "false",
        audioSource.autoplay ? "true" : "false",
        audioSource.enabled ? "true" : "false",
        audioSource.mute ? "true" : "false",
        audioSource.spatial ? "true" : "false",
        audioSource.pan,
        audioSource.spatialBlend,
        InspectorComponentLabelFormatter::AudioAttenuationModelName(audioSource.attenuationModel),
        audioSource.minDistance,
        audioSource.maxDistance,
        audioSource.rolloff,
        audioSource.dopplerFactor,
        static_cast<int>(outputBusDisplay.size()),
        outputBusDisplay.data());
    text += component;
}

void InspectorAudioListenerTextBuilder::Append(std::string& text, const kb::scene::AudioListenerComponent& audioListener) const {
    char component[224]{};
    std::snprintf(
        component,
        sizeof(component),
        "\n\nAudio Listener\nPriority: %d\nLocal User: %u\nPrimary: %s\nEnabled: %s",
        audioListener.priority,
        audioListener.localUser.value,
        audioListener.primary ? "true" : "false",
        audioListener.enabled ? "true" : "false");
    text += component;
}

} // namespace kb::editor
