#include "inspection/InspectorAudioMixerAssetModel.hpp"

#include <algorithm>
#include <iomanip>
#include <limits>
#include <locale>
#include <sstream>
#include <utility>

namespace kb::editor {
namespace {

constexpr int kFieldRowHeight = 24;
constexpr int kGroupRowHeight = 20;

[[nodiscard]] InspectorDynamicRowIdentity Identity(
    InspectorAudioMixerRowKind kind,
    InspectorPropertyId property,
    std::string_view bus,
    std::string_view snapshot,
    std::string_view overrideBus) {
    const std::uint32_t semanticKind = property == InspectorPropertyId::None
        ? 0x20000U + static_cast<std::uint32_t>(kind)
        : 0x10000U + static_cast<std::uint32_t>(property);
    return InspectorDynamicRowIdentity{
        .kind = semanticKind,
        .first = std::string{ bus },
        .second = std::string{ snapshot },
        .third = std::string{ overrideBus },
    };
}

[[nodiscard]] bool HasOverride(const kb::audio::AudioMixerSnapshot& snapshot, std::string_view bus) noexcept {
    return std::ranges::any_of(snapshot.busVolumes, [bus](const kb::audio::AudioMixerSnapshotBusVolume& value) {
        return value.bus == bus;
    });
}

[[nodiscard]] std::string FormatFloat(float value) {
    std::ostringstream output;
    output.imbue(std::locale::classic());
    output << std::setprecision(std::numeric_limits<float>::max_digits10) << value;
    return output.str();
}

[[nodiscard]] std::string UniqueName(std::string_view base, const auto& values, const auto& nameOf) {
    const auto exists = [&values, &nameOf](std::string_view candidate) {
        return std::ranges::any_of(values, [candidate, &nameOf](const auto& value) {
            return nameOf(value) == candidate;
        });
    };
    if (!exists(base)) {
        return std::string{ base };
    }
    for (std::size_t suffix = 1U;; ++suffix) {
        std::string candidate{ base };
        candidate += std::to_string(suffix);
        if (!exists(candidate)) {
            return candidate;
        }
    }
}

} // namespace

InspectorAudioMixerAssetModel::InspectorAudioMixerAssetModel(const kb::audio::AudioMixerAsset& asset) {
    const auto add = [this](InspectorAudioMixerRow row) {
        row.flatIndex = static_cast<int>(rows_.size());
        row.identity = Identity(row.kind, row.property, row.busName, row.snapshotName, row.overrideBusName);
        rows_.push_back(std::move(row));
    };

    if (asset.buses.empty()) {
        add(InspectorAudioMixerRow{
            .kind = InspectorAudioMixerRowKind::Empty,
            .section = InspectorSectionId::AudioMixerBuses,
            .label = "No buses",
        });
    }
    for (std::size_t busIndex = 0U; busIndex < asset.buses.size(); ++busIndex) {
        const kb::audio::AudioMixerBus& bus = asset.buses[busIndex];
        add(InspectorAudioMixerRow{
            .kind = InspectorAudioMixerRowKind::Group,
            .section = InspectorSectionId::AudioMixerBuses,
            .busIndex = busIndex,
            .busName = bus.name,
            .label = bus.name,
        });
        add(InspectorAudioMixerRow{
            .kind = InspectorAudioMixerRowKind::Text,
            .section = InspectorSectionId::AudioMixerBuses,
            .property = InspectorPropertyId::AudioMixerBusName,
            .busIndex = busIndex,
            .busName = bus.name,
            .label = "Name",
            .value = bus.name,
        });
        add(InspectorAudioMixerRow{
            .kind = InspectorAudioMixerRowKind::Text,
            .section = InspectorSectionId::AudioMixerBuses,
            .property = InspectorPropertyId::AudioMixerBusParent,
            .busIndex = busIndex,
            .busName = bus.name,
            .label = "Parent",
            .value = bus.parentBus.empty() ? "-" : bus.parentBus,
        });
        add(InspectorAudioMixerRow{
            .kind = InspectorAudioMixerRowKind::Text,
            .section = InspectorSectionId::AudioMixerBuses,
            .property = InspectorPropertyId::AudioMixerBusVolume,
            .busIndex = busIndex,
            .busName = bus.name,
            .label = "Volume",
            .value = FormatFloat(bus.volume),
        });
        add(InspectorAudioMixerRow{
            .kind = InspectorAudioMixerRowKind::Bool,
            .section = InspectorSectionId::AudioMixerBuses,
            .property = InspectorPropertyId::AudioMixerBusMute,
            .busIndex = busIndex,
            .busName = bus.name,
            .label = "Mute",
            .boolValue = bus.mute,
        });
        add(InspectorAudioMixerRow{
            .kind = InspectorAudioMixerRowKind::Action,
            .section = InspectorSectionId::AudioMixerBuses,
            .property = InspectorPropertyId::AudioMixerBusRemove,
            .busIndex = busIndex,
            .busName = bus.name,
            .label = "Remove Bus",
        });
    }
    add(InspectorAudioMixerRow{
        .kind = InspectorAudioMixerRowKind::Action,
        .section = InspectorSectionId::AudioMixerBuses,
        .property = InspectorPropertyId::AudioMixerBusAdd,
        .label = "Add Bus",
    });
    busRows_ = rows_.size();

    if (asset.snapshots.empty()) {
        add(InspectorAudioMixerRow{
            .kind = InspectorAudioMixerRowKind::Empty,
            .section = InspectorSectionId::AudioMixerSnapshots,
            .label = "No snapshots",
        });
    }
    for (std::size_t snapshotIndex = 0U; snapshotIndex < asset.snapshots.size(); ++snapshotIndex) {
        const kb::audio::AudioMixerSnapshot& snapshot = asset.snapshots[snapshotIndex];
        add(InspectorAudioMixerRow{
            .kind = InspectorAudioMixerRowKind::Group,
            .section = InspectorSectionId::AudioMixerSnapshots,
            .snapshotIndex = snapshotIndex,
            .snapshotName = snapshot.name,
            .label = snapshot.name,
        });
        add(InspectorAudioMixerRow{
            .kind = InspectorAudioMixerRowKind::Text,
            .section = InspectorSectionId::AudioMixerSnapshots,
            .property = InspectorPropertyId::AudioMixerSnapshotName,
            .snapshotIndex = snapshotIndex,
            .snapshotName = snapshot.name,
            .label = "Name",
            .value = snapshot.name,
        });
        add(InspectorAudioMixerRow{
            .kind = InspectorAudioMixerRowKind::Action,
            .section = InspectorSectionId::AudioMixerSnapshots,
            .property = InspectorPropertyId::AudioMixerSnapshotRemove,
            .snapshotIndex = snapshotIndex,
            .snapshotName = snapshot.name,
            .label = "Remove Snapshot",
        });

        if (asset.buses.empty()) {
            add(InspectorAudioMixerRow{
                .kind = InspectorAudioMixerRowKind::Empty,
                .section = InspectorSectionId::AudioMixerSnapshots,
                .snapshotIndex = snapshotIndex,
                .snapshotName = snapshot.name,
                .label = "No buses available",
            });
        }
        for (std::size_t overrideIndex = 0U; overrideIndex < snapshot.busVolumes.size(); ++overrideIndex) {
            const kb::audio::AudioMixerSnapshotBusVolume& value = snapshot.busVolumes[overrideIndex];
            add(InspectorAudioMixerRow{
                .kind = InspectorAudioMixerRowKind::Group,
                .section = InspectorSectionId::AudioMixerSnapshots,
                .snapshotIndex = snapshotIndex,
                .overrideIndex = overrideIndex,
                .snapshotName = snapshot.name,
                .overrideBusName = value.bus,
                .label = "Override " + value.bus,
            });
            add(InspectorAudioMixerRow{
                .kind = InspectorAudioMixerRowKind::ReadOnly,
                .section = InspectorSectionId::AudioMixerSnapshots,
                .property = InspectorPropertyId::AudioMixerOverrideBus,
                .snapshotIndex = snapshotIndex,
                .overrideIndex = overrideIndex,
                .snapshotName = snapshot.name,
                .overrideBusName = value.bus,
                .label = "Bus",
                .value = value.bus,
            });
            add(InspectorAudioMixerRow{
                .kind = InspectorAudioMixerRowKind::Text,
                .section = InspectorSectionId::AudioMixerSnapshots,
                .property = InspectorPropertyId::AudioMixerOverrideVolume,
                .snapshotIndex = snapshotIndex,
                .overrideIndex = overrideIndex,
                .snapshotName = snapshot.name,
                .overrideBusName = value.bus,
                .label = "Volume",
                .value = FormatFloat(value.volume),
            });
            add(InspectorAudioMixerRow{
                .kind = InspectorAudioMixerRowKind::Action,
                .section = InspectorSectionId::AudioMixerSnapshots,
                .property = InspectorPropertyId::AudioMixerOverrideRemove,
                .snapshotIndex = snapshotIndex,
                .overrideIndex = overrideIndex,
                .snapshotName = snapshot.name,
                .overrideBusName = value.bus,
                .label = "Remove Override",
            });
        }
        for (std::size_t busIndex = 0U; busIndex < asset.buses.size(); ++busIndex) {
            const kb::audio::AudioMixerBus& bus = asset.buses[busIndex];
            if (HasOverride(snapshot, bus.name)) {
                continue;
            }
            add(InspectorAudioMixerRow{
                .kind = InspectorAudioMixerRowKind::Action,
                .section = InspectorSectionId::AudioMixerSnapshots,
                .property = InspectorPropertyId::AudioMixerOverrideAdd,
                .busIndex = busIndex,
                .snapshotIndex = snapshotIndex,
                .busName = bus.name,
                .snapshotName = snapshot.name,
                .overrideBusName = bus.name,
                .label = "Add " + bus.name,
            });
        }
    }
    add(InspectorAudioMixerRow{
        .kind = InspectorAudioMixerRowKind::Action,
        .section = InspectorSectionId::AudioMixerSnapshots,
        .property = InspectorPropertyId::AudioMixerSnapshotAdd,
        .label = "Add Snapshot",
    });
}

std::span<const InspectorAudioMixerRow> InspectorAudioMixerAssetModel::Rows() const noexcept {
    return rows_;
}

std::span<const InspectorAudioMixerRow> InspectorAudioMixerAssetModel::Rows(InspectorSectionId section) const noexcept {
    if (section == InspectorSectionId::AudioMixerBuses) {
        return std::span<const InspectorAudioMixerRow>{ rows_ }.first(busRows_);
    }
    if (section == InspectorSectionId::AudioMixerSnapshots) {
        return std::span<const InspectorAudioMixerRow>{ rows_ }.subspan(busRows_);
    }
    return {};
}

const InspectorAudioMixerRow* InspectorAudioMixerAssetModel::Find(int flatIndex) const noexcept {
    if (flatIndex < 0 || static_cast<std::size_t>(flatIndex) >= rows_.size()) {
        return nullptr;
    }
    return &rows_[static_cast<std::size_t>(flatIndex)];
}

const InspectorAudioMixerRow* InspectorAudioMixerAssetModel::Find(
    int flatIndex,
    const InspectorDynamicRowIdentity& identity) const noexcept {
    const InspectorAudioMixerRow* row = Find(flatIndex);
    return row != nullptr && row->identity == identity ? row : nullptr;
}

std::string InspectorAudioMixerAssetModel::Text() const {
    std::ostringstream text;
    InspectorSectionId section = InspectorSectionId::None;
    for (const InspectorAudioMixerRow& row : rows_) {
        if (row.section != section) {
            section = row.section;
            text << '\n' << (section == InspectorSectionId::AudioMixerBuses ? "Buses" : "Snapshots") << '\n';
        }
        switch (row.kind) {
        case InspectorAudioMixerRowKind::Group:
            text << '[' << row.label << "]\n";
            break;
        case InspectorAudioMixerRowKind::Text:
        case InspectorAudioMixerRowKind::ReadOnly:
            text << row.label << ": " << row.value << '\n';
            break;
        case InspectorAudioMixerRowKind::Bool:
            text << row.label << ": " << (row.boolValue ? "true" : "false") << '\n';
            break;
        case InspectorAudioMixerRowKind::Empty:
            text << row.label << '\n';
            break;
        case InspectorAudioMixerRowKind::Action:
            break;
        }
    }
    return text.str();
}

std::string InspectorAudioMixerAssetModel::UniqueBusName(const kb::audio::AudioMixerAsset& asset) {
    return UniqueName("Bus", asset.buses, [](const kb::audio::AudioMixerBus& bus) -> std::string_view { return bus.name; });
}

std::string InspectorAudioMixerAssetModel::UniqueSnapshotName(const kb::audio::AudioMixerAsset& asset) {
    return UniqueName("Snapshot", asset.snapshots, [](const kb::audio::AudioMixerSnapshot& snapshot) -> std::string_view { return snapshot.name; });
}

int InspectorAudioMixerAssetModel::RowHeight(InspectorAudioMixerRowKind kind) noexcept {
    return kind == InspectorAudioMixerRowKind::Group ? kGroupRowHeight : kFieldRowHeight;
}

} // namespace kb::editor
