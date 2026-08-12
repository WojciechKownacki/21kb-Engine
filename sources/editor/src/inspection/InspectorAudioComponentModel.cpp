#include "inspection/InspectorAudioComponentModel.hpp"

#include "engine/assets/AssetHandle.hpp"
#include "engine/assets/AssetId.hpp"
#include "engine/assets/AssetKind.hpp"
#include "engine/assets/AssetMetadata.hpp"
#include "engine/audio/AudioMixerAsset.hpp"
#include "engine/scene/AudioListenerComponent.hpp"
#include "engine/scene/AudioSourceComponent.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneAssets.hpp"
#include "engine/scene/SceneAudioMixerAccess.hpp"
#include "engine/scene/SceneComponents.hpp"
#include "engine/scene/SceneComponentQueries.hpp"
#include "engine/scene/SceneEntities.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <limits>

namespace kb::editor {
namespace {

constexpr std::array<InspectorAudioRow, 17U> kSourceRows{ {
    { InspectorPropertyId::AudioSourceClip, InspectorAudioControlKind::Asset, "Clip", InspectorPropertyId::AudioSourceClipPicker },
    { InspectorPropertyId::AudioSourceClipClear, InspectorAudioControlKind::Action, "Clear Clip" },
    { InspectorPropertyId::AudioSourceVolume, InspectorAudioControlKind::Float, "Volume" },
    { InspectorPropertyId::AudioSourcePitch, InspectorAudioControlKind::Float, "Pitch" },
    { InspectorPropertyId::AudioSourceEnabled, InspectorAudioControlKind::Bool, "Enabled" },
    { InspectorPropertyId::AudioSourceAutoplay, InspectorAudioControlKind::Bool, "Autoplay" },
    { InspectorPropertyId::AudioSourceLoop, InspectorAudioControlKind::Bool, "Loop" },
    { InspectorPropertyId::AudioSourceMute, InspectorAudioControlKind::Bool, "Mute" },
    { InspectorPropertyId::AudioSourceSpatial, InspectorAudioControlKind::Bool, "Spatial" },
    { InspectorPropertyId::AudioSourcePan, InspectorAudioControlKind::Float, "Pan" },
    { InspectorPropertyId::AudioSourceSpatialBlend, InspectorAudioControlKind::Float, "Spatial Blend" },
    { InspectorPropertyId::AudioSourceAttenuation, InspectorAudioControlKind::Enum, "Attenuation" },
    { InspectorPropertyId::AudioSourceMinDistance, InspectorAudioControlKind::Float, "Min Distance" },
    { InspectorPropertyId::AudioSourceMaxDistance, InspectorAudioControlKind::Float, "Max Distance" },
    { InspectorPropertyId::AudioSourceRolloff, InspectorAudioControlKind::Float, "Rolloff" },
    { InspectorPropertyId::AudioSourceDopplerFactor, InspectorAudioControlKind::Float, "Doppler Factor" },
    { InspectorPropertyId::AudioSourceOutputBus, InspectorAudioControlKind::Route, "Output Bus" },
} };

constexpr std::array<InspectorAudioRow, 4U> kListenerRows{ {
    { InspectorPropertyId::AudioListenerPriority, InspectorAudioControlKind::Integer, "Priority" },
    { InspectorPropertyId::AudioListenerLocalUser, InspectorAudioControlKind::Integer, "Local User" },
    { InspectorPropertyId::AudioListenerPrimary, InspectorAudioControlKind::Bool, "Primary" },
    { InspectorPropertyId::AudioListenerEnabled, InspectorAudioControlKind::Bool, "Enabled" },
} };

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
    if (text.empty()) {
        return false;
    }
    const std::from_chars_result parsed = std::from_chars(text.data(), text.data() + text.size(), value);
    return parsed.ec == std::errc{} && parsed.ptr == text.data() + text.size() && std::isfinite(value);
}

[[nodiscard]] bool ParseInteger(std::string_view text, std::int64_t& value) noexcept {
    text = Trim(text);
    if (text.empty()) {
        return false;
    }
    const std::from_chars_result parsed = std::from_chars(text.data(), text.data() + text.size(), value);
    return parsed.ec == std::errc{} && parsed.ptr == text.data() + text.size();
}

[[nodiscard]] bool SetSourceFloat(kb::scene::AudioSourceComponent& source, InspectorPropertyId property, float value) noexcept {
    float* target = nullptr;
    switch (property) {
    case InspectorPropertyId::AudioSourceVolume: target = &source.volume; break;
    case InspectorPropertyId::AudioSourcePitch: target = &source.pitch; break;
    case InspectorPropertyId::AudioSourcePan: target = &source.pan; break;
    case InspectorPropertyId::AudioSourceSpatialBlend: target = &source.spatialBlend; break;
    case InspectorPropertyId::AudioSourceMinDistance: target = &source.minDistance; break;
    case InspectorPropertyId::AudioSourceMaxDistance: target = &source.maxDistance; break;
    case InspectorPropertyId::AudioSourceRolloff: target = &source.rolloff; break;
    case InspectorPropertyId::AudioSourceDopplerFactor: target = &source.dopplerFactor; break;
    default: return false;
    }
    if (*target == value) {
        return false;
    }
    *target = value;
    return true;
}

[[nodiscard]] bool SetSourceBool(kb::scene::AudioSourceComponent& source, InspectorPropertyId property) noexcept {
    switch (property) {
    case InspectorPropertyId::AudioSourceEnabled: source.enabled = !source.enabled; break;
    case InspectorPropertyId::AudioSourceAutoplay: source.autoplay = !source.autoplay; break;
    case InspectorPropertyId::AudioSourceLoop: source.loop = !source.loop; break;
    case InspectorPropertyId::AudioSourceMute: source.mute = !source.mute; break;
    case InspectorPropertyId::AudioSourceSpatial: source.spatial = !source.spatial; break;
    default: return false;
    }
    return true;
}

} // namespace

std::span<const InspectorAudioRow> InspectorAudioComponentModel::SourceRows() noexcept {
    return kSourceRows;
}

std::span<const InspectorAudioRow> InspectorAudioComponentModel::ListenerRows() noexcept {
    return kListenerRows;
}

const InspectorAudioRow* InspectorAudioComponentModel::FindRow(InspectorPropertyId property) noexcept {
    const auto find = [property](std::span<const InspectorAudioRow> rows) -> const InspectorAudioRow* {
        const auto found = std::ranges::find_if(rows, [property](const InspectorAudioRow& row) { return row.property == property; });
        return found == rows.end() ? nullptr : &*found;
    };
    if (const InspectorAudioRow* source = find(SourceRows())) {
        return source;
    }
    return find(ListenerRows());
}

InspectorHitKind InspectorAudioComponentModel::HitKindForControl(InspectorAudioControlKind kind) noexcept {
    switch (kind) {
    case InspectorAudioControlKind::Bool: return InspectorHitKind::BoolField;
    case InspectorAudioControlKind::Float:
    case InspectorAudioControlKind::Integer: return InspectorHitKind::FloatField;
    case InspectorAudioControlKind::Asset:
    case InspectorAudioControlKind::Enum:
    case InspectorAudioControlKind::Route:
    case InspectorAudioControlKind::Action: return InspectorHitKind::TextField;
    }
    return InspectorHitKind::None;
}

bool InspectorAudioComponentModel::HasRemoveControl(InspectorSectionId section) noexcept {
    return section == InspectorSectionId::AudioSource || section == InspectorSectionId::AudioListener;
}

bool InspectorAudioComponentModel::RemoveComponent(kb::scene::Scene& scene, kb::scene::SceneEntity entity, InspectorSectionId section) noexcept {
    if (!scene.Entities().IsAlive(entity)) {
        return false;
    }
    if (section == InspectorSectionId::AudioSource && scene.Components().AudioSources().Has(entity)) {
        scene.Components().AudioSources().Remove(entity);
        return true;
    }
    if (section == InspectorSectionId::AudioListener && scene.Components().AudioListeners().Has(entity)) {
        scene.Components().AudioListeners().Remove(entity);
        return true;
    }
    return false;
}

bool InspectorAudioComponentModel::IsFloatProperty(InspectorPropertyId property) noexcept {
    const InspectorAudioRow* row = FindRow(property);
    return row != nullptr && row->kind == InspectorAudioControlKind::Float;
}

bool InspectorAudioComponentModel::IsIntegerProperty(InspectorPropertyId property) noexcept {
    const InspectorAudioRow* row = FindRow(property);
    return row != nullptr && row->kind == InspectorAudioControlKind::Integer;
}

bool InspectorAudioComponentModel::ReadFloat(const kb::scene::Scene& scene, kb::scene::SceneEntity entity, InspectorPropertyId property, float& value) noexcept {
    const kb::scene::AudioSourceComponent* source = scene.Components().AudioSources().TryGet(entity);
    if (source == nullptr) {
        return false;
    }
    switch (property) {
    case InspectorPropertyId::AudioSourceVolume: value = source->volume; break;
    case InspectorPropertyId::AudioSourcePitch: value = source->pitch; break;
    case InspectorPropertyId::AudioSourcePan: value = source->pan; break;
    case InspectorPropertyId::AudioSourceSpatialBlend: value = source->spatialBlend; break;
    case InspectorPropertyId::AudioSourceMinDistance: value = source->minDistance; break;
    case InspectorPropertyId::AudioSourceMaxDistance: value = source->maxDistance; break;
    case InspectorPropertyId::AudioSourceRolloff: value = source->rolloff; break;
    case InspectorPropertyId::AudioSourceDopplerFactor: value = source->dopplerFactor; break;
    default: return false;
    }
    return std::isfinite(value);
}

bool InspectorAudioComponentModel::ReadInteger(const kb::scene::Scene& scene, kb::scene::SceneEntity entity, InspectorPropertyId property, std::int64_t& value) noexcept {
    const kb::scene::AudioListenerComponent* listener = scene.Components().AudioListeners().TryGet(entity);
    if (listener == nullptr) {
        return false;
    }
    switch (property) {
    case InspectorPropertyId::AudioListenerPriority: value = listener->priority; return true;
    case InspectorPropertyId::AudioListenerLocalUser: value = listener->localUser.value; return true;
    default: return false;
    }
}

bool InspectorAudioComponentModel::ApplyFloat(kb::scene::Scene& scene, kb::scene::SceneEntity entity, InspectorPropertyId property, float value) noexcept {
    if (!scene.Entities().IsAlive(entity)) {
        return false;
    }
    kb::scene::AudioSourceComponent* source = scene.Components().AudioSources().TryGet(entity);
    if (source == nullptr) {
        return false;
    }
    kb::scene::AudioSourceComponent candidate = *source;
    if (!SetSourceFloat(candidate, property, value) || !kb::scene::IsAudioSourceComponentPersistable(candidate)) {
        return false;
    }
    *source = candidate;
    scene.Components().AudioSources().MarkModified(entity);
    return true;
}

bool InspectorAudioComponentModel::ApplyInteger(kb::scene::Scene& scene, kb::scene::SceneEntity entity, InspectorPropertyId property, std::int64_t value) noexcept {
    if (!scene.Entities().IsAlive(entity)) {
        return false;
    }
    kb::scene::AudioListenerComponent* listener = scene.Components().AudioListeners().TryGet(entity);
    if (listener == nullptr) {
        return false;
    }
    switch (property) {
    case InspectorPropertyId::AudioListenerPriority:
        if (value < std::numeric_limits<std::int32_t>::min() || value > std::numeric_limits<std::int32_t>::max()) return false;
        if (listener->priority == static_cast<std::int32_t>(value)) return false;
        listener->priority = static_cast<std::int32_t>(value);
        break;
    case InspectorPropertyId::AudioListenerLocalUser:
        if (value < 0 || static_cast<std::uint64_t>(value) > std::numeric_limits<std::uint32_t>::max()) return false;
        if (listener->localUser.value == static_cast<std::uint32_t>(value)) return false;
        listener->localUser.value = static_cast<std::uint32_t>(value);
        break;
    default: return false;
    }
    scene.Components().AudioListeners().MarkModified(entity);
    return true;
}

bool InspectorAudioComponentModel::ApplyText(kb::scene::Scene& scene, kb::scene::SceneEntity entity, InspectorPropertyId property, std::string_view text) noexcept {
    if (IsFloatProperty(property)) {
        float value = 0.0F;
        return ParseFloat(text, value) && ApplyFloat(scene, entity, property, value);
    }
    if (IsIntegerProperty(property)) {
        std::int64_t value = 0;
        return ParseInteger(text, value) && ApplyInteger(scene, entity, property, value);
    }
    return false;
}

bool InspectorAudioComponentModel::Toggle(kb::scene::Scene& scene, kb::scene::SceneEntity entity, InspectorPropertyId property) noexcept {
    if (!scene.Entities().IsAlive(entity)) {
        return false;
    }
    if (kb::scene::AudioSourceComponent* source = scene.Components().AudioSources().TryGet(entity); source != nullptr) {
        kb::scene::AudioSourceComponent candidate = *source;
        if (SetSourceBool(candidate, property) && kb::scene::IsAudioSourceComponentPersistable(candidate)) {
            *source = candidate;
            scene.Components().AudioSources().MarkModified(entity);
            return true;
        }
    }
    kb::scene::AudioListenerComponent* listener = scene.Components().AudioListeners().TryGet(entity);
    if (listener == nullptr) {
        return false;
    }
    switch (property) {
    case InspectorPropertyId::AudioListenerEnabled: listener->enabled = !listener->enabled; break;
    case InspectorPropertyId::AudioListenerPrimary: listener->primary = !listener->primary; break;
    default: return false;
    }
    scene.Components().AudioListeners().MarkModified(entity);
    return true;
}

bool InspectorAudioComponentModel::CycleAttenuation(kb::scene::Scene& scene, kb::scene::SceneEntity entity) noexcept {
    if (!scene.Entities().IsAlive(entity)) {
        return false;
    }
    kb::scene::AudioSourceComponent* source = scene.Components().AudioSources().TryGet(entity);
    if (source == nullptr) {
        return false;
    }
    kb::scene::AudioSourceComponent candidate = *source;
    switch (candidate.attenuationModel) {
    case kb::audio::AudioAttenuationModel::None: candidate.attenuationModel = kb::audio::AudioAttenuationModel::Inverse; break;
    case kb::audio::AudioAttenuationModel::Inverse: candidate.attenuationModel = kb::audio::AudioAttenuationModel::Linear; break;
    case kb::audio::AudioAttenuationModel::Linear: candidate.attenuationModel = kb::audio::AudioAttenuationModel::Exponential; break;
    case kb::audio::AudioAttenuationModel::Exponential: candidate.attenuationModel = kb::audio::AudioAttenuationModel::None; break;
    default: return false;
    }
    if (!kb::scene::IsAudioSourceComponentPersistable(candidate)) {
        return false;
    }
    *source = candidate;
    scene.Components().AudioSources().MarkModified(entity);
    return true;
}

std::string InspectorAudioComponentModel::ClipDisplay(const kb::scene::Scene& scene, const kb::scene::AudioSourceComponent& source) {
    const kb::assets::AssetId clipId{ source.clipAssetId };
    if (!clipId.IsValid()) {
        return "(none)";
    }
    const kb::assets::AssetMetadata* metadata = scene.Assets().Manager().Registry().Find(clipId);
    if (metadata == nullptr) {
        return "(missing)";
    }
    if (!kb::assets::AssetMatchesKind(*metadata, kb::assets::AssetKind::Audio)) {
        return "(invalid audio asset)";
    }
    return metadata->name.empty() ? metadata->virtualPath.filename().string() : metadata->name;
}

InspectorAudioRouteChoices InspectorAudioComponentModel::RouteChoices(const kb::scene::Scene& scene) {
    InspectorAudioRouteChoices choices{};
    choices.names.emplace_back();
    const std::uint64_t mixerId = kb::scene::SceneAudioMixerAccess::ActiveMixer(scene);
    if (mixerId == 0U) {
        return choices;
    }
    const kb::assets::AssetMetadata* metadata = scene.Assets().Manager().Registry().Find(kb::assets::AssetId{ mixerId });
    if (metadata == nullptr || metadata->type != kb::audio::kAudioMixerAssetType) {
        choices.status = InspectorAudioRouteStatus::Unavailable;
        return choices;
    }
    const kb::assets::AssetHandle<kb::audio::AudioMixerAsset> mixer = scene.Assets().Manager().Load<kb::audio::AudioMixerAsset>(kb::assets::AssetId{ mixerId });
    if (!mixer.IsLoaded()) {
        choices.status = InspectorAudioRouteStatus::Unavailable;
        return choices;
    }
    choices.status = InspectorAudioRouteStatus::Available;
    choices.names.reserve(mixer->buses.size() + 1U);
    for (const kb::audio::AudioMixerBus& bus : mixer->buses) {
        choices.names.push_back(bus.name);
    }
    return choices;
}

bool InspectorAudioComponentModel::SetOutputBus(kb::scene::Scene& scene, kb::scene::SceneEntity entity, std::string_view busName) {
    if (!scene.Entities().IsAlive(entity)) {
        return false;
    }
    kb::scene::AudioSourceComponent* source = scene.Components().AudioSources().TryGet(entity);
    if (source == nullptr) {
        return false;
    }
    const InspectorAudioRouteChoices choices = RouteChoices(scene);
    if (!std::ranges::any_of(choices.names, [busName](const std::string& name) { return name == busName; })) {
        return false;
    }
    if (kb::scene::AudioSourceOutputBus(*source) == busName) {
        return false;
    }
    kb::scene::AudioSourceComponent candidate = *source;
    if (!kb::scene::SetAudioSourceOutputBus(candidate, busName) || !kb::scene::IsAudioSourceComponentPersistable(candidate)) {
        return false;
    }
    *source = candidate;
    scene.Components().AudioSources().MarkModified(entity);
    return true;
}

bool InspectorAudioComponentModel::CycleOutputBus(kb::scene::Scene& scene, kb::scene::SceneEntity entity) {
    kb::scene::AudioSourceComponent* source = scene.Components().AudioSources().TryGet(entity);
    if (source == nullptr) {
        return false;
    }
    const InspectorAudioRouteChoices choices = RouteChoices(scene);
    const std::string_view current = kb::scene::AudioSourceOutputBus(*source);
    const auto found = std::ranges::find(choices.names, current);
    const std::size_t next = found == choices.names.end()
        ? 0U
        : (static_cast<std::size_t>(found - choices.names.begin()) + 1U) % choices.names.size();
    if (choices.names[next] == current) {
        return false;
    }
    return SetOutputBus(scene, entity, choices.names[next]);
}

std::string InspectorAudioComponentModel::OutputBusDisplay(const kb::scene::Scene& scene, const kb::scene::AudioSourceComponent& source) {
    if (!kb::scene::IsAudioSourceOutputBusValid(source)) {
        return "(invalid)";
    }
    const std::string_view current = kb::scene::AudioSourceOutputBus(source);
    if (current.empty()) {
        return "(master)";
    }
    const InspectorAudioRouteChoices choices = RouteChoices(scene);
    if (std::ranges::any_of(choices.names, [current](const std::string& name) { return name == current; })) {
        return std::string{ current };
    }
    return std::string{ current } + " (missing)";
}

} // namespace kb::editor
