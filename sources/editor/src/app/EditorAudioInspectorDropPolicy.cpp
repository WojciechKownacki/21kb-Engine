#include "app/EditorAudioInspectorDropPolicy.hpp"

#include "engine/audio/AudioMixerAsset.hpp"
#include "scene/EditorSceneAudioAssetActions.hpp"

namespace kb::editor {

bool EditorAudioInspectorDropPolicy::Accepts(
    const kb::assets::AssetMetadata& metadata,
    InspectorSectionId section,
    InspectorPropertyId property) noexcept {
    if (section == InspectorSectionId::SceneAudioRouting
        && (property == InspectorPropertyId::SceneAudioMixer
            || property == InspectorPropertyId::SceneAudioMixerPicker)) {
        return metadata.type == kb::audio::kAudioMixerAssetType;
    }
    if (section == InspectorSectionId::AudioSource
        && (property == InspectorPropertyId::AudioSourceClip
            || property == InspectorPropertyId::AudioSourceClipPicker)) {
        return EditorSceneAudioAssetActions::IsAudioAsset(metadata);
    }
    return false;
}

} // namespace kb::editor
