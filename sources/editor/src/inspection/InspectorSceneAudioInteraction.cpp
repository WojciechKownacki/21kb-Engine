#include "inspection/InspectorSceneAudioInteraction.hpp"

#include "engine/library/EngineLibraryParsing.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneAudioOcclusionAccess.hpp"

#include <charconv>
#include <cmath>
#include <limits>
#include <string_view>

namespace kb::editor {
namespace {

[[nodiscard]] std::string_view Trim(std::string_view text) noexcept {
    while (!text.empty() && (text.front() == ' ' || text.front() == '\t')) {
        text.remove_prefix(1U);
    }
    while (!text.empty() && (text.back() == ' ' || text.back() == '\t')) {
        text.remove_suffix(1U);
    }
    return text;
}

[[nodiscard]] bool ParseFloat(std::string_view text, float& value) noexcept {
    text = Trim(text);
    double parsed = 0.0;
    if (text.empty() || !kb::library::TryParseDouble(text, parsed)
        || !std::isfinite(parsed)
        || parsed < -static_cast<double>(std::numeric_limits<float>::max())
        || parsed > static_cast<double>(std::numeric_limits<float>::max())) {
        return false;
    }
    value = static_cast<float>(parsed);
    return std::isfinite(value);
}

[[nodiscard]] bool ParseUnsigned(std::string_view text, std::uint32_t& value) noexcept {
    text = Trim(text);
    if (text.empty()) {
        return false;
    }
    const std::from_chars_result parsed =
        std::from_chars(text.data(), text.data() + text.size(), value);
    return parsed.ec == std::errc{} && parsed.ptr == text.data() + text.size();
}

[[nodiscard]] std::optional<InspectorSceneAudioCommand> OcclusionCommand(
    const kb::scene::Scene& scene,
    InspectorPropertyId property,
    std::string_view text) {
    InspectorSceneAudioCommand command{
        .kind = InspectorSceneAudioCommandKind::SetOcclusion,
        .occlusion = kb::scene::SceneAudioOcclusionAccess::Settings(scene),
    };
    float floatValue = 0.0F;
    std::uint32_t unsignedValue = 0U;
    switch (property) {
    case InspectorPropertyId::SceneAudioOcclusionVolumeScale:
        if (!ParseFloat(text, floatValue) || floatValue < 0.0F || floatValue > 1.0F) {
            return std::nullopt;
        }
        command.occlusion.occludedVolumeScale = floatValue;
        break;
    case InspectorPropertyId::SceneAudioOcclusionMaxDistance:
        if (!ParseFloat(text, floatValue) || floatValue < 0.0F) {
            return std::nullopt;
        }
        command.occlusion.maxDistance = floatValue;
        break;
    case InspectorPropertyId::SceneAudioOcclusionLayerMask:
        if (!ParseUnsigned(text, unsignedValue)) {
            return std::nullopt;
        }
        command.occlusion.layerMask = unsignedValue;
        break;
    case InspectorPropertyId::SceneAudioOcclusionMaxRaycasts:
        if (!ParseUnsigned(text, unsignedValue)
            || unsignedValue > kb::scene::kMaxAudioOcclusionRaycastsPerTick) {
            return std::nullopt;
        }
        command.occlusion.maxRaycastsPerTick = unsignedValue;
        break;
    default:
        return std::nullopt;
    }
    return kb::scene::IsAudioOcclusionSettingsValid(command.occlusion)
        ? std::optional<InspectorSceneAudioCommand>{ std::move(command) }
        : std::nullopt;
}

} // namespace

bool InspectorSceneAudioInteraction::IsSection(InspectorSectionId section) noexcept {
    return section == InspectorSectionId::SceneAudioRouting
        || section == InspectorSectionId::SceneAudioOcclusion;
}

bool InspectorSceneAudioInteraction::IsTextProperty(InspectorPropertyId property) noexcept {
    return property == InspectorPropertyId::SceneAudioOcclusionVolumeScale
        || property == InspectorPropertyId::SceneAudioOcclusionMaxDistance
        || property == InspectorPropertyId::SceneAudioOcclusionLayerMask
        || property == InspectorPropertyId::SceneAudioOcclusionMaxRaycasts;
}

bool InspectorSceneAudioInteraction::BeginTextEdit(
    InspectorPanelState& state,
    const InspectorSceneAudioModel& model,
    const InspectorSceneAudioTarget& target,
    std::uint64_t documentGeneration) {
    const InspectorSceneAudioRow* row = model.Find(target.index);
    if (target.kind != InspectorHitKind::TextField
        || row == nullptr
        || row->kind != InspectorSceneAudioRowKind::Text
        || row->section != target.section
        || row->property != target.property
        || !IsTextProperty(row->property)) {
        return false;
    }
    InspectorDynamicRowIdentity identity = row->identity;
    identity.ownerDocumentGeneration = documentGeneration;
    state.BeginTextEdit(row->property, row->value, row->flatIndex, std::move(identity));
    return true;
}

std::optional<InspectorSceneAudioCommand> InspectorSceneAudioInteraction::ResolveAction(
    const kb::scene::Scene& scene,
    const InspectorSceneAudioModel& model,
    const InspectorSceneAudioTarget& target) {
    const InspectorSceneAudioRow* row = model.Find(target.index);
    if (row == nullptr || row->section != target.section || row->property != target.property) {
        return std::nullopt;
    }
    if (row->property == InspectorPropertyId::SceneAudioMixerClear
        && row->kind == InspectorSceneAudioRowKind::Action
        && target.kind == InspectorHitKind::Row) {
        return InspectorSceneAudioCommand{
            .kind = InspectorSceneAudioCommandKind::SetMixer,
        };
    }
    if (row->property == InspectorPropertyId::SceneAudioSnapshotOption
        && row->kind == InspectorSceneAudioRowKind::Action
        && target.kind == InspectorHitKind::Row) {
        return InspectorSceneAudioCommand{
            .kind = InspectorSceneAudioCommandKind::SetSnapshot,
            .snapshot = row->option,
        };
    }
    if (row->property == InspectorPropertyId::SceneAudioOcclusionEnabled
        && row->kind == InspectorSceneAudioRowKind::Bool
        && target.kind == InspectorHitKind::BoolField) {
        kb::scene::AudioOcclusionSettings settings =
            kb::scene::SceneAudioOcclusionAccess::Settings(scene);
        settings.enabled = !settings.enabled;
        return InspectorSceneAudioCommand{
            .kind = InspectorSceneAudioCommandKind::SetOcclusion,
            .occlusion = settings,
        };
    }
    return std::nullopt;
}

std::optional<InspectorSceneAudioCommand> InspectorSceneAudioInteraction::ResolvePicker(
    bool accepted,
    kb::assets::AssetId mixer) noexcept {
    return accepted
        ? std::optional<InspectorSceneAudioCommand>{ InspectorSceneAudioCommand{
              .kind = InspectorSceneAudioCommandKind::SetMixer,
              .mixer = mixer,
          } }
        : std::nullopt;
}

std::optional<kb::assets::AssetId> InspectorSceneAudioInteraction::ResolveReveal(
    const InspectorSceneAudioModel& model,
    const InspectorSceneAudioTarget& target) noexcept {
    const InspectorSceneAudioRow* row = model.Find(target.index);
    return row != nullptr
            && row->kind == InspectorSceneAudioRowKind::Asset
            && row->section == target.section
            && row->property == target.property
            && target.kind == InspectorHitKind::Row
            && model.MixerId().IsValid()
        ? std::optional<kb::assets::AssetId>{ model.MixerId() }
        : std::nullopt;
}

std::optional<InspectorSceneAudioCommand> InspectorSceneAudioInteraction::ResolveCommit(
    const InspectorPanelState& state,
    const kb::scene::Scene& scene,
    const InspectorSceneAudioModel& model,
    std::uint64_t documentGeneration) {
    if (!IsTextProperty(state.EditedProperty())
        || !state.EditRowIdentity().has_value()
        || state.EditRowIdentity()->ownerDocumentGeneration != documentGeneration) {
        return std::nullopt;
    }
    InspectorDynamicRowIdentity semantic = *state.EditRowIdentity();
    semantic.ownerDocumentGeneration = 0U;
    const InspectorSceneAudioRow* row = model.Find(state.EditIndex(), semantic);
    if (row == nullptr
        || row->kind != InspectorSceneAudioRowKind::Text
        || row->property != state.EditedProperty()
        || row->value != state.EditOriginalBuffer()
        || state.EditBuffer() == state.EditOriginalBuffer()) {
        return std::nullopt;
    }
    return OcclusionCommand(scene, row->property, state.EditBuffer());
}

} // namespace kb::editor
