#pragma once

#include "engine/assets/AssetId.hpp"
#include "inspection/InspectorAudioMixerAssetModel.hpp"

#include <optional>
#include <string>

namespace kb::editor {

struct InspectorAudioMixerAssetTarget {
    InspectorHitKind kind = InspectorHitKind::None;
    InspectorSectionId section = InspectorSectionId::None;
    InspectorPropertyId property = InspectorPropertyId::None;
    int index = -1;
};

enum class InspectorAudioMixerCommandKind : std::uint8_t {
    AddBus,
    RemoveBus,
    RenameBus,
    SetBusParent,
    SetBusVolume,
    SetBusMute,
    AddSnapshot,
    RemoveSnapshot,
    RenameSnapshot,
    AddOverride,
    RemoveOverride,
    SetOverrideVolume,
};

struct InspectorAudioMixerCommand {
    InspectorAudioMixerCommandKind kind = InspectorAudioMixerCommandKind::AddBus;
    std::string bus;
    std::string snapshot;
    std::string value;
    float number = 0.0F;
    bool flag = false;
};

class InspectorAudioMixerAssetInteraction {
public:
    InspectorAudioMixerAssetInteraction() = delete;

    [[nodiscard]] static bool IsTextProperty(InspectorPropertyId property) noexcept;
    [[nodiscard]] static bool BeginTextEdit(
        InspectorPanelState& state,
        const kb::audio::AudioMixerAsset& asset,
        const InspectorAudioMixerAssetTarget& target,
        kb::assets::AssetId id);
    [[nodiscard]] static std::optional<InspectorAudioMixerCommand> ResolveAction(
        const kb::audio::AudioMixerAsset& asset,
        const InspectorAudioMixerAssetTarget& target);
    [[nodiscard]] static std::optional<InspectorAudioMixerCommand> ResolveCommit(
        const InspectorPanelState& state,
        const kb::audio::AudioMixerAsset& asset,
        kb::assets::AssetId id);

    template <typename Actions>
    [[nodiscard]] static bool Apply(
        Actions& actions,
        kb::assets::AssetId id,
        const InspectorAudioMixerCommand& command) {
        switch (command.kind) {
        case InspectorAudioMixerCommandKind::AddBus:
            return actions.AddBus(id, command.value);
        case InspectorAudioMixerCommandKind::RemoveBus:
            return actions.RemoveBus(id, command.bus);
        case InspectorAudioMixerCommandKind::RenameBus:
            return actions.RenameBus(id, command.bus, command.value);
        case InspectorAudioMixerCommandKind::SetBusParent:
            return actions.SetBusParent(id, command.bus, command.value);
        case InspectorAudioMixerCommandKind::SetBusVolume:
            return actions.SetBusVolume(id, command.bus, command.number);
        case InspectorAudioMixerCommandKind::SetBusMute:
            return actions.SetBusMute(id, command.bus, command.flag);
        case InspectorAudioMixerCommandKind::AddSnapshot:
            return actions.AddSnapshot(id, command.value);
        case InspectorAudioMixerCommandKind::RemoveSnapshot:
            return actions.RemoveSnapshot(id, command.snapshot);
        case InspectorAudioMixerCommandKind::RenameSnapshot:
            return actions.RenameSnapshot(id, command.snapshot, command.value);
        case InspectorAudioMixerCommandKind::AddOverride:
            return actions.AddSnapshotOverride(id, command.snapshot, command.bus, command.number);
        case InspectorAudioMixerCommandKind::RemoveOverride:
            return actions.RemoveSnapshotOverride(id, command.snapshot, command.bus);
        case InspectorAudioMixerCommandKind::SetOverrideVolume:
            return actions.SetSnapshotOverrideVolume(id, command.snapshot, command.bus, command.number);
        }
        return false;
    }

    template <typename Actions>
    [[nodiscard]] static bool HandlePointerDown(
        InspectorPanelState& state,
        const kb::audio::AudioMixerAsset& asset,
        const InspectorAudioMixerAssetTarget& target,
        Actions& actions,
        kb::assets::AssetId id) {
        if (BeginTextEdit(state, asset, target, id)) {
            return true;
        }
        const InspectorAudioMixerAssetModel model{ asset };
        const InspectorAudioMixerRow* row = model.Find(target.index);
        if (row == nullptr || row->section != target.section || row->property != target.property) {
            return false;
        }
        state.EndTextEdit();
        if (const std::optional<InspectorAudioMixerCommand> command = ResolveAction(asset, target)) {
            static_cast<void>(Apply(actions, id, *command));
        }
        return true;
    }

    template <typename Actions>
    [[nodiscard]] static bool CommitTextEdit(
        InspectorPanelState& state,
        const kb::audio::AudioMixerAsset& asset,
        Actions& actions,
        kb::assets::AssetId id) {
        const std::optional<InspectorAudioMixerCommand> command = ResolveCommit(state, asset, id);
        state.EndTextEdit();
        return command.has_value() && Apply(actions, id, *command);
    }
};

} // namespace kb::editor
