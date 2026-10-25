#include "inspection/InspectorAudioMixerRuntimeInteraction.hpp"

#include <utility>

namespace kb::editor {
bool InspectorAudioMixerRuntimeInteraction::HandlePointerDown(kb::scene::Scene& scene, InspectorPanelState& state,
    const kb::audio::AudioMixerAsset& asset, const InspectorAudioMixerAssetTarget& target,
    kb::assets::AssetId id, const EditorAudioMixerRuntimeState& runtime) {
    const InspectorAudioMixerAssetModel model{asset, &runtime};
    const auto* row = model.Find(target.index);
    if (row == nullptr || row->section != InspectorSectionId::AudioMixerRuntime
        || row->property != target.property || row->section != target.section) return false;
    if (row->kind == InspectorAudioMixerRowKind::Text && target.kind == InspectorHitKind::TextField
        && row->property == InspectorPropertyId::AudioMixerPreviewClip) {
        auto identity = row->identity; identity.ownerAssetId = id.value;
        state.BeginTextEdit(row->property, row->value, row->flatIndex, std::move(identity));
        return true;
    }
    state.EndTextEdit();
    if (row->kind == InspectorAudioMixerRowKind::Bool && target.kind == InspectorHitKind::BoolField
        && row->property == InspectorPropertyId::AudioMixerCapture) {
        static_cast<void>(EditorAudioMixerRuntime::ConfigureCapture(scene, id, !runtime.meters.captureEnabled));
    } else if (row->kind == InspectorAudioMixerRowKind::Action && target.kind == InspectorHitKind::Row) {
        if (row->property == InspectorPropertyId::AudioMixerResetMeters)
            static_cast<void>(EditorAudioMixerRuntime::ResetCapture(scene, id));
        else if (row->property == InspectorPropertyId::AudioMixerPreviewBus)
            static_cast<void>(EditorAudioMixerRuntime::Play(scene, id, row->busName));
        else if (row->property == InspectorPropertyId::AudioMixerStopPreview) EditorAudioMixerRuntime::Stop(scene);
    }
    return true;
}

bool InspectorAudioMixerRuntimeInteraction::CommitTextEdit(kb::scene::Scene& scene, InspectorPanelState& state,
    const kb::audio::AudioMixerAsset& asset, kb::assets::AssetId id, const EditorAudioMixerRuntimeState& runtime) {
    const InspectorAudioMixerAssetModel model{asset, &runtime};
    bool valid = state.EditedProperty() == InspectorPropertyId::AudioMixerPreviewClip && state.EditRowIdentity().has_value()
        && state.EditRowIdentity()->ownerAssetId == id.value;
    if (valid) {
        auto identity = *state.EditRowIdentity(); identity.ownerAssetId = 0U;
        const auto* row = model.Find(state.EditIndex(), identity);
        valid = row != nullptr && row->property == InspectorPropertyId::AudioMixerPreviewClip;
    }
    const std::string reference{state.EditBuffer()};
    state.EndTextEdit();
    return valid && EditorAudioMixerRuntime::SetPreviewClip(scene, id, reference);
}
} // namespace kb::editor
