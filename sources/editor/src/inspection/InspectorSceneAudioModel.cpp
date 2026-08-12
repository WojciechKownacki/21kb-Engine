#include "inspection/InspectorSceneAudioModel.hpp"

#include "engine/assets/AssetManager.hpp"
#include "engine/audio/AudioMixerAsset.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneAssets.hpp"
#include "engine/scene/SceneAudioMixerAccess.hpp"
#include "engine/scene/SceneEntities.hpp"

#include <iomanip>
#include <limits>
#include <locale>
#include <sstream>
#include <utility>

namespace kb::editor {
namespace {

constexpr int kFieldRowHeight = 24;

[[nodiscard]] std::string FormatFloat(float value) {
    std::ostringstream output;
    output.imbue(std::locale::classic());
    output << std::setprecision(std::numeric_limits<float>::max_digits10) << value;
    return output.str();
}

[[nodiscard]] InspectorDynamicRowIdentity Identity(
    InspectorSceneAudioRowKind kind,
    InspectorPropertyId property,
    std::string_view option,
    kb::assets::AssetId mixerId) {
    return InspectorDynamicRowIdentity{
        .kind = property == InspectorPropertyId::None
            ? 0x40000U + static_cast<std::uint32_t>(kind)
            : 0x30000U + static_cast<std::uint32_t>(property),
        .first = std::string{ option },
        .second = std::to_string(mixerId.value),
    };
}

} // namespace

InspectorSceneAudioModel::InspectorSceneAudioModel(const kb::scene::Scene& scene)
    : mixerId_(kb::scene::SceneAudioMixerAccess::ActiveMixer(scene)) {
    const auto add = [this](InspectorSceneAudioRow row) {
        row.flatIndex = static_cast<int>(rows_.size());
        row.identity = Identity(row.kind, row.property, row.option, mixerId_);
        rows_.push_back(std::move(row));
    };

    kb::assets::AssetHandle<kb::audio::AudioMixerAsset> mixer;
    const kb::assets::AssetMetadata* mixerMetadata = nullptr;
    if (mixerId_.IsValid()) {
        const kb::assets::AssetManager& manager = scene.Assets().Manager();
        mixerMetadata = manager.Registry().Find(mixerId_);
        if (mixerMetadata != nullptr && mixerMetadata->type == kb::audio::kAudioMixerAssetType) {
            mixer = manager.AcquireLoaded<kb::audio::AudioMixerAsset>(mixerId_);
            if (mixer.IsLoaded() && !kb::audio::ValidateAudioMixerAsset(*mixer).empty()) {
                mixer = {};
            }
        }
    }

    std::string mixerValue = "None";
    bool mixerInvalid = false;
    if (mixerId_.IsValid()) {
        mixerInvalid = mixerMetadata == nullptr
            || mixerMetadata->type != kb::audio::kAudioMixerAssetType
            || !mixer.IsLoaded();
        mixerValue = mixerMetadata != nullptr && !mixerMetadata->name.empty()
            ? mixerMetadata->name
            : "Missing (" + std::to_string(mixerId_.value) + ")";
    }
    add(InspectorSceneAudioRow{
        .kind = InspectorSceneAudioRowKind::Asset,
        .section = InspectorSectionId::SceneAudioRouting,
        .property = InspectorPropertyId::SceneAudioMixer,
        .label = "Mixer",
        .value = std::move(mixerValue),
        .invalid = mixerInvalid,
    });
    if (mixerId_.IsValid()) {
        add(InspectorSceneAudioRow{
            .kind = InspectorSceneAudioRowKind::Action,
            .section = InspectorSectionId::SceneAudioRouting,
            .property = InspectorPropertyId::SceneAudioMixerClear,
            .label = "Clear Mixer",
        });
    }

    const std::string& activeSnapshot = kb::scene::SceneAudioMixerAccess::ActiveSnapshot(scene);
    const bool snapshotValid = activeSnapshot.empty()
        || (mixer.IsLoaded() && mixer->FindSnapshot(activeSnapshot) != nullptr);
    add(InspectorSceneAudioRow{
        .kind = InspectorSceneAudioRowKind::ReadOnly,
        .section = InspectorSectionId::SceneAudioRouting,
        .property = InspectorPropertyId::SceneAudioSnapshot,
        .label = "Snapshot",
        .value = activeSnapshot.empty() ? "Default" : activeSnapshot,
        .invalid = !snapshotValid,
    });
    if (!activeSnapshot.empty()) {
        add(InspectorSceneAudioRow{
            .kind = InspectorSceneAudioRowKind::Action,
            .section = InspectorSectionId::SceneAudioRouting,
            .property = InspectorPropertyId::SceneAudioSnapshotOption,
            .label = "Use Default",
        });
    }
    if (mixer.IsLoaded()) {
        for (const kb::audio::AudioMixerSnapshot& snapshot : mixer->snapshots) {
            if (snapshot.name == activeSnapshot) {
                continue;
            }
            add(InspectorSceneAudioRow{
                .kind = InspectorSceneAudioRowKind::Action,
                .section = InspectorSectionId::SceneAudioRouting,
                .property = InspectorPropertyId::SceneAudioSnapshotOption,
                .label = "Use " + snapshot.name,
                .option = snapshot.name,
            });
        }
    } else if (!mixerId_.IsValid()) {
        add(InspectorSceneAudioRow{
            .kind = InspectorSceneAudioRowKind::Empty,
            .section = InspectorSectionId::SceneAudioRouting,
            .label = "Assign a mixer to use snapshots",
        });
    }
    routingRows_ = rows_.size();

    const kb::scene::AudioOcclusionSettings& settings =
        kb::scene::SceneAudioOcclusionAccess::Settings(scene);
    add(InspectorSceneAudioRow{
        .kind = InspectorSceneAudioRowKind::Bool,
        .section = InspectorSectionId::SceneAudioOcclusion,
        .property = InspectorPropertyId::SceneAudioOcclusionEnabled,
        .label = "Enabled",
        .boolValue = settings.enabled,
    });
    add(InspectorSceneAudioRow{
        .kind = InspectorSceneAudioRowKind::Text,
        .section = InspectorSectionId::SceneAudioOcclusion,
        .property = InspectorPropertyId::SceneAudioOcclusionVolumeScale,
        .label = "Volume Scale",
        .value = FormatFloat(settings.occludedVolumeScale),
    });
    add(InspectorSceneAudioRow{
        .kind = InspectorSceneAudioRowKind::Text,
        .section = InspectorSectionId::SceneAudioOcclusion,
        .property = InspectorPropertyId::SceneAudioOcclusionMaxDistance,
        .label = "Max Distance",
        .value = FormatFloat(settings.maxDistance),
    });
    add(InspectorSceneAudioRow{
        .kind = InspectorSceneAudioRowKind::Text,
        .section = InspectorSectionId::SceneAudioOcclusion,
        .property = InspectorPropertyId::SceneAudioOcclusionLayerMask,
        .label = "Layer Mask",
        .value = std::to_string(settings.layerMask),
    });
    add(InspectorSceneAudioRow{
        .kind = InspectorSceneAudioRowKind::Text,
        .section = InspectorSectionId::SceneAudioOcclusion,
        .property = InspectorPropertyId::SceneAudioOcclusionMaxRaycasts,
        .label = "Max Raycasts",
        .value = std::to_string(settings.maxRaycastsPerTick),
    });
}

std::span<const InspectorSceneAudioRow> InspectorSceneAudioModel::Rows() const noexcept {
    return rows_;
}

std::span<const InspectorSceneAudioRow> InspectorSceneAudioModel::Rows(
    InspectorSectionId section) const noexcept {
    if (section == InspectorSectionId::SceneAudioRouting) {
        return std::span<const InspectorSceneAudioRow>{ rows_ }.first(routingRows_);
    }
    if (section == InspectorSectionId::SceneAudioOcclusion) {
        return std::span<const InspectorSceneAudioRow>{ rows_ }.subspan(routingRows_);
    }
    return {};
}

const InspectorSceneAudioRow* InspectorSceneAudioModel::Find(int flatIndex) const noexcept {
    if (flatIndex < 0 || static_cast<std::size_t>(flatIndex) >= rows_.size()) {
        return nullptr;
    }
    return &rows_[static_cast<std::size_t>(flatIndex)];
}

const InspectorSceneAudioRow* InspectorSceneAudioModel::Find(
    int flatIndex,
    const InspectorDynamicRowIdentity& identity) const noexcept {
    const InspectorSceneAudioRow* row = Find(flatIndex);
    return row != nullptr && row->identity == identity ? row : nullptr;
}

kb::assets::AssetId InspectorSceneAudioModel::MixerId() const noexcept {
    return mixerId_;
}

std::string InspectorSceneAudioModel::Text() const {
    std::ostringstream text;
    text << "Scene Audio\n";
    InspectorSectionId section = InspectorSectionId::None;
    for (const InspectorSceneAudioRow& row : rows_) {
        if (row.section != section) {
            section = row.section;
            text << (section == InspectorSectionId::SceneAudioRouting ? "Routing" : "Occlusion") << '\n';
        }
        switch (row.kind) {
        case InspectorSceneAudioRowKind::Asset:
        case InspectorSceneAudioRowKind::ReadOnly:
        case InspectorSceneAudioRowKind::Text:
            text << row.label << ": " << row.value << (row.invalid ? " (invalid)" : "") << '\n';
            break;
        case InspectorSceneAudioRowKind::Bool:
            text << row.label << ": " << (row.boolValue ? "true" : "false") << '\n';
            break;
        case InspectorSceneAudioRowKind::Empty:
            text << row.label << '\n';
            break;
        case InspectorSceneAudioRowKind::Action:
            text << "Action: " << row.label << '\n';
            break;
        }
    }
    return text.str();
}

int InspectorSceneAudioModel::RowHeight(InspectorSceneAudioRowKind) noexcept {
    return kFieldRowHeight;
}

bool InspectorSceneAudioModel::ShouldDisplay(
    const kb::scene::Scene& scene,
    kb::assets::AssetId selectedAsset,
    kb::scene::SceneEntity selectedEntity) noexcept {
    return !selectedAsset.IsValid() && !scene.Entities().IsAlive(selectedEntity);
}

} // namespace kb::editor
