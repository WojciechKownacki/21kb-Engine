#include "inspection/InspectorAudioMixerAssetInteraction.hpp"

#include "engine/library/EngineLibraryParsing.hpp"

#include <cmath>
#include <limits>
#include <string_view>
#include <utility>

namespace kb::editor {
namespace {

[[nodiscard]] bool ParseVolume(std::string_view text, float& value) noexcept {
    if (text.empty()) {
        return false;
    }
    double parsed = 0.0;
    if (!kb::library::TryParseDouble(text, parsed)
        || !std::isfinite(parsed)
        || parsed < 0.0
        || parsed > static_cast<double>(std::numeric_limits<float>::max())) {
        return false;
    }
    value = static_cast<float>(parsed);
    return std::isfinite(value);
}

} // namespace

bool InspectorAudioMixerAssetInteraction::IsTextProperty(InspectorPropertyId property) noexcept {
    switch (property) {
    case InspectorPropertyId::AudioMixerBusName:
    case InspectorPropertyId::AudioMixerBusParent:
    case InspectorPropertyId::AudioMixerBusVolume:
    case InspectorPropertyId::AudioMixerSnapshotName:
    case InspectorPropertyId::AudioMixerOverrideVolume:
        return true;
    default:
        return false;
    }
}

bool InspectorAudioMixerAssetInteraction::BeginTextEdit(
    InspectorPanelState& state,
    const kb::audio::AudioMixerAsset& asset,
    const InspectorAudioMixerAssetTarget& target,
    kb::assets::AssetId id) {
    if (target.kind != InspectorHitKind::TextField || !IsTextProperty(target.property)) {
        return false;
    }
    const InspectorAudioMixerAssetModel model{ asset };
    const InspectorAudioMixerRow* row = model.Find(target.index);
    if (row == nullptr || row->kind != InspectorAudioMixerRowKind::Text
        || row->section != target.section || row->property != target.property) {
        return false;
    }
    InspectorDynamicRowIdentity identity = row->identity;
    identity.ownerAssetId = id.value;
    state.BeginTextEdit(row->property, row->value, row->flatIndex, std::move(identity));
    return true;
}

std::optional<InspectorAudioMixerCommand> InspectorAudioMixerAssetInteraction::ResolveAction(
    const kb::audio::AudioMixerAsset& asset,
    const InspectorAudioMixerAssetTarget& target) {
    const InspectorAudioMixerAssetModel model{ asset };
    const InspectorAudioMixerRow* row = model.Find(target.index);
    if (row == nullptr || row->section != target.section || row->property != target.property) {
        return std::nullopt;
    }
    switch (row->property) {
    case InspectorPropertyId::AudioMixerBusAdd:
        if (row->kind == InspectorAudioMixerRowKind::Action && target.kind == InspectorHitKind::Row) {
            return InspectorAudioMixerCommand{
                .kind = InspectorAudioMixerCommandKind::AddBus,
                .value = InspectorAudioMixerAssetModel::UniqueBusName(asset),
            };
        }
        break;
    case InspectorPropertyId::AudioMixerBusRemove:
        if (row->kind == InspectorAudioMixerRowKind::Action && target.kind == InspectorHitKind::Row) {
            return InspectorAudioMixerCommand{ .kind = InspectorAudioMixerCommandKind::RemoveBus, .bus = row->busName };
        }
        break;
    case InspectorPropertyId::AudioMixerBusMute:
        if (row->kind == InspectorAudioMixerRowKind::Bool && target.kind == InspectorHitKind::BoolField) {
            return InspectorAudioMixerCommand{
                .kind = InspectorAudioMixerCommandKind::SetBusMute,
                .bus = row->busName,
                .flag = !row->boolValue,
            };
        }
        break;
    case InspectorPropertyId::AudioMixerSnapshotAdd:
        if (row->kind == InspectorAudioMixerRowKind::Action && target.kind == InspectorHitKind::Row) {
            return InspectorAudioMixerCommand{
                .kind = InspectorAudioMixerCommandKind::AddSnapshot,
                .value = InspectorAudioMixerAssetModel::UniqueSnapshotName(asset),
            };
        }
        break;
    case InspectorPropertyId::AudioMixerSnapshotRemove:
        if (row->kind == InspectorAudioMixerRowKind::Action && target.kind == InspectorHitKind::Row) {
            return InspectorAudioMixerCommand{ .kind = InspectorAudioMixerCommandKind::RemoveSnapshot, .snapshot = row->snapshotName };
        }
        break;
    case InspectorPropertyId::AudioMixerOverrideAdd:
        if (row->kind == InspectorAudioMixerRowKind::Action && target.kind == InspectorHitKind::Row) {
            return InspectorAudioMixerCommand{
                .kind = InspectorAudioMixerCommandKind::AddOverride,
                .bus = row->overrideBusName,
                .snapshot = row->snapshotName,
                .number = 1.0F,
            };
        }
        break;
    case InspectorPropertyId::AudioMixerOverrideRemove:
        if (row->kind == InspectorAudioMixerRowKind::Action && target.kind == InspectorHitKind::Row) {
            return InspectorAudioMixerCommand{
                .kind = InspectorAudioMixerCommandKind::RemoveOverride,
                .bus = row->overrideBusName,
                .snapshot = row->snapshotName,
            };
        }
        break;
    default:
        break;
    }
    return std::nullopt;
}

std::optional<InspectorAudioMixerCommand> InspectorAudioMixerAssetInteraction::ResolveCommit(
    const InspectorPanelState& state,
    const kb::audio::AudioMixerAsset& asset,
    kb::assets::AssetId id) {
    if (!state.IsTextEditing() || !state.IsTextEditDirty() || !IsTextProperty(state.EditedProperty())
        || !state.EditRowIdentity().has_value()) {
        return std::nullopt;
    }
    const InspectorAudioMixerAssetModel model{ asset };
    InspectorDynamicRowIdentity expected = *state.EditRowIdentity();
    if (expected.ownerAssetId != id.value) {
        return std::nullopt;
    }
    expected.ownerAssetId = 0U;
    const InspectorAudioMixerRow* row = model.Find(state.EditIndex(), expected);
    if (row == nullptr || row->kind != InspectorAudioMixerRowKind::Text
        || row->property != state.EditedProperty()
        || row->value != state.EditOriginalBuffer()) {
        return std::nullopt;
    }
    switch (row->property) {
    case InspectorPropertyId::AudioMixerBusName:
        return InspectorAudioMixerCommand{
            .kind = InspectorAudioMixerCommandKind::RenameBus,
            .bus = row->busName,
            .value = state.EditBuffer(),
        };
    case InspectorPropertyId::AudioMixerBusParent: {
        const std::string parent = state.EditBuffer().empty() || state.EditBuffer() == "-"
            ? std::string{}
            : state.EditBuffer();
        return InspectorAudioMixerCommand{
            .kind = InspectorAudioMixerCommandKind::SetBusParent,
            .bus = row->busName,
            .value = parent,
        };
    }
    case InspectorPropertyId::AudioMixerBusVolume: {
        float value = 0.0F;
        if (!ParseVolume(state.EditBuffer(), value)) {
            return std::nullopt;
        }
        return InspectorAudioMixerCommand{
            .kind = InspectorAudioMixerCommandKind::SetBusVolume,
            .bus = row->busName,
            .number = value,
        };
    }
    case InspectorPropertyId::AudioMixerSnapshotName:
        return InspectorAudioMixerCommand{
            .kind = InspectorAudioMixerCommandKind::RenameSnapshot,
            .snapshot = row->snapshotName,
            .value = state.EditBuffer(),
        };
    case InspectorPropertyId::AudioMixerOverrideVolume: {
        float value = 0.0F;
        if (!ParseVolume(state.EditBuffer(), value)) {
            return std::nullopt;
        }
        return InspectorAudioMixerCommand{
            .kind = InspectorAudioMixerCommandKind::SetOverrideVolume,
            .bus = row->overrideBusName,
            .snapshot = row->snapshotName,
            .number = value,
        };
    }
    default:
        return std::nullopt;
    }
}

} // namespace kb::editor
