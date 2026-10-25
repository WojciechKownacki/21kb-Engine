#pragma once

#include "inspection/InspectorAudioMixerAssetInteraction.hpp"
#include "scene/audio/EditorAudioMixerRuntime.hpp"

namespace kb::editor {
class InspectorAudioMixerRuntimeInteraction {
public:
    [[nodiscard]] static bool HandlePointerDown(kb::scene::Scene& scene, InspectorPanelState& state,
        const kb::audio::AudioMixerAsset& asset, const InspectorAudioMixerAssetTarget& target,
        kb::assets::AssetId id, const EditorAudioMixerRuntimeState& runtime);
    [[nodiscard]] static bool CommitTextEdit(kb::scene::Scene& scene, InspectorPanelState& state,
        const kb::audio::AudioMixerAsset& asset, kb::assets::AssetId id, const EditorAudioMixerRuntimeState& runtime);
};
} // namespace kb::editor
