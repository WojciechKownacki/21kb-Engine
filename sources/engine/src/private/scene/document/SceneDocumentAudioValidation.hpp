#pragma once

#include "engine/audio/AudioMixerAsset.hpp"
#include "engine/scene/SceneDocument.hpp"

namespace kb::scene {

[[nodiscard]] inline bool IsSceneDocumentAudioConfigurationValid(const SceneDocument& document) noexcept {
    return (document.audioMixerSnapshot.empty()
            || (document.audioMixerAssetId != 0U
                && kb::audio::IsAudioMixerNameTokenValid(document.audioMixerSnapshot)))
        && IsAudioOcclusionSettingsValid(document.audioOcclusionSettings);
}

} // namespace kb::scene
