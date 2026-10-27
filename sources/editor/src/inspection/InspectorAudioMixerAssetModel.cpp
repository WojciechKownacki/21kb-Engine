#include "inspection/InspectorAudioMixerAssetModel.hpp"
#include "localization/EditorLocalization.hpp"
#include "engine/audio/AudioMixerEffectEditing.hpp"
#include "scene/audio/EditorAudioMixerRuntime.hpp"

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

[[nodiscard]] std::string EffectKindLabel(kb::audio::AudioDspKind kind) {
    using Kind = kb::audio::AudioDspKind;
    switch (kind) {
    case Kind::LowPass: return std::string{EditorText("inspector.audioMixer.dsp.lowpass", "Low-pass Filter")};
    case Kind::HighPass: return std::string{EditorText("inspector.audioMixer.dsp.highpass", "High-pass Filter")};
    case Kind::PeakingEqualizer: return std::string{EditorText("inspector.audioMixer.dsp.equalizer", "Equalizer")};
    case Kind::Delay: return std::string{EditorText("inspector.audioMixer.dsp.delay", "Delay")};
    case Kind::Reverb: return std::string{EditorText("inspector.audioMixer.dsp.reverb", "Reverb")};
    case Kind::Compressor: return std::string{EditorText("inspector.audioMixer.dsp.compressor", "Compressor")};
    case Kind::Limiter: return std::string{EditorText("inspector.audioMixer.dsp.limiter", "Limiter")};
    }
    return {};
}

[[nodiscard]] std::string EffectFieldLabel(std::string_view field) {
    if (field == "bypass") return std::string{EditorText("inspector.audioMixer.dsp.bypass", "Bypass")};
    if (field == "cutoffHz") return std::string{EditorText("inspector.audioMixer.dsp.cutoffHz", "Frequency (Hz)")};
    if (field == "order") return std::string{EditorText("inspector.audioMixer.dsp.order", "Filter Order")};
    if (field == "gainDb") return std::string{EditorText("inspector.audioMixer.dsp.gainDb", "Gain (dB)")};
    if (field == "quality") return std::string{EditorText("inspector.audioMixer.dsp.quality", "Q")};
    if (field == "delayMs") return std::string{EditorText("inspector.audioMixer.dsp.delayMs", "Delay (ms)")};
    if (field == "feedback") return std::string{EditorText("inspector.audioMixer.dsp.feedback", "Feedback")};
    if (field == "wet") return std::string{EditorText("inspector.audioMixer.dsp.wet", "Wet Gain")};
    if (field == "dry") return std::string{EditorText("inspector.audioMixer.dsp.dry", "Dry Gain")};
    if (field == "roomSize") return std::string{EditorText("inspector.audioMixer.dsp.roomSize", "Room Size")};
    if (field == "damping") return std::string{EditorText("inspector.audioMixer.dsp.damping", "Damping")};
    if (field == "width") return std::string{EditorText("inspector.audioMixer.dsp.width", "Stereo Width")};
    if (field == "thresholdDb") return std::string{EditorText("inspector.audioMixer.dsp.thresholdDb", "Threshold (dBFS)")};
    if (field == "ratio") return std::string{EditorText("inspector.audioMixer.dsp.ratio", "Ratio")};
    if (field == "attackMs") return std::string{EditorText("inspector.audioMixer.dsp.attackMs", "Attack (ms)")};
    if (field == "releaseMs") return std::string{EditorText("inspector.audioMixer.dsp.releaseMs", "Release (ms)")};
    if (field == "makeupDb") return std::string{EditorText("inspector.audioMixer.dsp.makeupDb", "Makeup Gain (dB)")};
    if (field == "ceilingDb") return std::string{EditorText("inspector.audioMixer.dsp.ceilingDb", "Ceiling (dBFS)")};
    return std::string{field};
}

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

InspectorAudioMixerAssetModel::InspectorAudioMixerAssetModel(const kb::audio::AudioMixerAsset& asset,
    const EditorAudioMixerRuntimeState* runtime) {
    const auto add = [this](InspectorAudioMixerRow row) {
        row.flatIndex = static_cast<int>(rows_.size());
        row.identity = Identity(row.kind, row.property, row.busName,
            row.effectId.empty() && row.property != InspectorPropertyId::AudioMixerEffectAdd ? row.snapshotName : row.effectId,
            row.effectId.empty() && row.property != InspectorPropertyId::AudioMixerEffectAdd ? row.overrideBusName : row.effectField);
        rows_.push_back(std::move(row));
    };

    if (asset.buses.empty()) {
        add(InspectorAudioMixerRow{
            .kind = InspectorAudioMixerRowKind::Empty,
            .section = InspectorSectionId::AudioMixerBuses,
            .label = std::string{ EditorText("inspector.audioMixer.noBuses", "No buses") },
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
            .label = std::string{ EditorText("inspector.audioMixer.busName", "Name") },
            .value = bus.name,
        });
        add(InspectorAudioMixerRow{
            .kind = InspectorAudioMixerRowKind::Text,
            .section = InspectorSectionId::AudioMixerBuses,
            .property = InspectorPropertyId::AudioMixerBusParent,
            .busIndex = busIndex,
            .busName = bus.name,
            .label = std::string{ EditorText("inspector.audioMixer.parent", "Parent") },
            .value = bus.parentBus.empty() ? "-" : bus.parentBus,
        });
        add(InspectorAudioMixerRow{
            .kind = InspectorAudioMixerRowKind::Text,
            .section = InspectorSectionId::AudioMixerBuses,
            .property = InspectorPropertyId::AudioMixerBusVolume,
            .busIndex = busIndex,
            .busName = bus.name,
            .label = std::string{ EditorText("inspector.audioMixer.volume", "Volume") },
            .value = FormatFloat(bus.volume),
        });
        add(InspectorAudioMixerRow{
            .kind = InspectorAudioMixerRowKind::Bool,
            .section = InspectorSectionId::AudioMixerBuses,
            .property = InspectorPropertyId::AudioMixerBusMute,
            .busIndex = busIndex,
            .busName = bus.name,
            .label = std::string{ EditorText("inspector.audioMixer.mute", "Mute") },
            .boolValue = bus.mute,
        });
        const auto effectRow = [&](const kb::audio::AudioDspEffect& effect, std::size_t index,
                InspectorAudioMixerRowKind kind, InspectorPropertyId property, std::string label,
                std::string_view field = {}) {
            InspectorAudioMixerRow row;
            row.kind = kind; row.section = InspectorSectionId::AudioMixerBuses; row.property = property;
            row.busIndex = busIndex; row.busName = bus.name; row.effectId = effect.id;
            row.effectField = field; row.effectIndex = index; row.effectKind = effect.parameters.kind;
            row.label = std::move(label);
            if (!field.empty()) row.value = kb::audio::AudioDspFieldValue(effect.parameters, field);
            row.boolValue = effect.parameters.bypass;
            add(std::move(row));
        };
        for (std::size_t index = 0U; index < bus.effects.size(); ++index) {
            const auto& effect = bus.effects[index];
            effectRow(effect, index, InspectorAudioMixerRowKind::Group, InspectorPropertyId::None,
                effect.id + ": " + EffectKindLabel(effect.parameters.kind));
            for (const auto field : kb::audio::AudioDspFieldNames(effect.parameters.kind)) {
                effectRow(effect, index, field == "bypass" ? InspectorAudioMixerRowKind::Bool : InspectorAudioMixerRowKind::Text,
                    field == "bypass" ? InspectorPropertyId::AudioMixerEffectBypass : InspectorPropertyId::AudioMixerEffectParameter,
                    EffectFieldLabel(field), field);
            }
            if (index > 0U) effectRow(effect, index, InspectorAudioMixerRowKind::Action, InspectorPropertyId::AudioMixerEffectMoveUp,
                std::string{EditorText("inspector.audioMixer.dsp.moveUp", "Move Up")});
            if (index + 1U < bus.effects.size()) effectRow(effect, index, InspectorAudioMixerRowKind::Action, InspectorPropertyId::AudioMixerEffectMoveDown,
                std::string{EditorText("inspector.audioMixer.dsp.moveDown", "Move Down")});
            effectRow(effect, index, InspectorAudioMixerRowKind::Action, InspectorPropertyId::AudioMixerEffectRemove,
                std::string{EditorText("inspector.audioMixer.dsp.remove", "Remove Effect")});
        }
        if (bus.effects.size() < 16U) {
            using Kind = kb::audio::AudioDspKind;
            for (const auto kind : {Kind::LowPass, Kind::HighPass, Kind::PeakingEqualizer, Kind::Delay, Kind::Reverb, Kind::Compressor, Kind::Limiter}) {
                InspectorAudioMixerRow row;
                row.kind = InspectorAudioMixerRowKind::Action; row.section = InspectorSectionId::AudioMixerBuses;
                row.property = InspectorPropertyId::AudioMixerEffectAdd; row.busIndex = busIndex; row.busName = bus.name;
                row.effectKind = kind; row.effectField = kb::audio::AudioDspKindName(kind);
                row.label = EditorLocalization::Format("inspector.audioMixer.dsp.add", "Add {kind}", {{"kind", EffectKindLabel(kind)}});
                add(std::move(row));
            }
        }
        add(InspectorAudioMixerRow{
            .kind = InspectorAudioMixerRowKind::Action,
            .section = InspectorSectionId::AudioMixerBuses,
            .property = InspectorPropertyId::AudioMixerBusRemove,
            .busIndex = busIndex,
            .busName = bus.name,
            .label = std::string{ EditorText("inspector.audioMixer.removeBus", "Remove Bus") },
        });
    }
    add(InspectorAudioMixerRow{
        .kind = InspectorAudioMixerRowKind::Action,
        .section = InspectorSectionId::AudioMixerBuses,
        .property = InspectorPropertyId::AudioMixerBusAdd,
        .label = std::string{ EditorText("inspector.audioMixer.addBus", "Add Bus") },
    });
    busRows_ = rows_.size();

    if (asset.snapshots.empty()) {
        add(InspectorAudioMixerRow{
            .kind = InspectorAudioMixerRowKind::Empty,
            .section = InspectorSectionId::AudioMixerSnapshots,
            .label = std::string{ EditorText("inspector.audioMixer.noSnapshots", "No snapshots") },
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
            .label = std::string{ EditorText("inspector.audioMixer.snapshotName", "Name") },
            .value = snapshot.name,
        });
        add(InspectorAudioMixerRow{
            .kind = InspectorAudioMixerRowKind::Action,
            .section = InspectorSectionId::AudioMixerSnapshots,
            .property = InspectorPropertyId::AudioMixerSnapshotRemove,
            .snapshotIndex = snapshotIndex,
            .snapshotName = snapshot.name,
            .label = std::string{ EditorText("inspector.audioMixer.removeSnapshot", "Remove Snapshot") },
        });

        if (asset.buses.empty()) {
            add(InspectorAudioMixerRow{
                .kind = InspectorAudioMixerRowKind::Empty,
                .section = InspectorSectionId::AudioMixerSnapshots,
                .snapshotIndex = snapshotIndex,
                .snapshotName = snapshot.name,
                .label = std::string{ EditorText("inspector.audioMixer.noBusesAvailable", "No buses available") },
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
                .label = EditorLocalization::Format("inspector.audioMixer.override", "Override {bus}", { { "bus", value.bus } }),
            });
            add(InspectorAudioMixerRow{
                .kind = InspectorAudioMixerRowKind::ReadOnly,
                .section = InspectorSectionId::AudioMixerSnapshots,
                .property = InspectorPropertyId::AudioMixerOverrideBus,
                .snapshotIndex = snapshotIndex,
                .overrideIndex = overrideIndex,
                .snapshotName = snapshot.name,
                .overrideBusName = value.bus,
                .label = std::string{ EditorText("inspector.audioMixer.bus", "Bus") },
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
                .label = std::string{ EditorText("inspector.audioMixer.volume", "Volume") },
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
                .label = std::string{ EditorText("inspector.audioMixer.removeOverride", "Remove Override") },
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
                .label = EditorLocalization::Format("inspector.audioMixer.addOverride", "Add {bus}", { { "bus", bus.name } }),
            });
        }
    }
    add(InspectorAudioMixerRow{
        .kind = InspectorAudioMixerRowKind::Action,
        .section = InspectorSectionId::AudioMixerSnapshots,
        .property = InspectorPropertyId::AudioMixerSnapshotAdd,
        .label = std::string{ EditorText("inspector.audioMixer.addSnapshot", "Add Snapshot") },
    });
    snapshotRows_ = rows_.size() - busRows_;
    if (runtime != nullptr) AppendRuntime(asset, *runtime);
}

void InspectorAudioMixerAssetModel::AppendRuntime(const kb::audio::AudioMixerAsset& asset, const EditorAudioMixerRuntimeState& runtime) {
    using Kind = InspectorAudioMixerRowKind;
    using Property = InspectorPropertyId;
    const auto add = [&](InspectorAudioMixerRow row) {
        row.section = InspectorSectionId::AudioMixerRuntime;
        row.flatIndex = static_cast<int>(rows_.size());
        row.identity = Identity(row.kind, row.property, row.busName,
            row.property == Property::AudioMixerPreviewClip ? runtime.previewClip : std::string{}, {});
        rows_.push_back(std::move(row));
    };
    add({.kind = Kind::Empty, .label = runtime.diagnostic.empty()
        ? std::string{runtime.meters.captureEnabled ? EditorText("inspector.audioMixer.capturing", "Capturing signal")
            : EditorText("inspector.audioMixer.captureDisabled", "Capture is disabled")}
        : runtime.diagnostic});
    add({.kind = runtime.canCapture ? Kind::Bool : Kind::ReadOnly, .property = Property::AudioMixerCapture,
        .label = std::string{EditorText("inspector.audioMixer.capture", "Capture signal")}, .boolValue = runtime.meters.captureEnabled});
    add({.kind = runtime.canCapture ? Kind::Action : Kind::ReadOnly, .property = Property::AudioMixerResetMeters,
        .label = std::string{EditorText("inspector.audioMixer.resetMeters", "Reset peaks and counters")}});
    add({.kind = Kind::Text, .property = Property::AudioMixerPreviewClip,
        .label = std::string{EditorText("inspector.audioMixer.previewClip", "Preview clip")}, .value = runtime.previewClip});
    add({.kind = runtime.previewPlaying ? Kind::Action : Kind::ReadOnly, .property = Property::AudioMixerStopPreview,
        .label = std::string{EditorText("inspector.audioMixer.stopPreview", "Stop preview")}});
    const auto meterRows = [&](std::string_view name, const kb::audio::AudioMeterReading* reading) {
        add({.kind = Kind::Group, .busName = std::string{name}, .label = name.empty()
            ? std::string{EditorText("inspector.audioMixer.masterOutput", "Master output")} : std::string{name}});
        add({.kind = runtime.canPreview && !runtime.previewClip.empty() ? Kind::Action : Kind::ReadOnly,
            .property = Property::AudioMixerPreviewBus, .busName = std::string{name},
            .label = std::string{EditorText("inspector.audioMixer.previewBus", "Preview through this bus")}});
        for (const auto property : {Property::AudioMixerMeterPeak, Property::AudioMixerMeterRms,
                Property::AudioMixerMeterHold, Property::AudioMixerMeterClips}) {
            std::string label;
            if (property == Property::AudioMixerMeterPeak) label = EditorText("inspector.audioMixer.peak", "Peak");
            else if (property == Property::AudioMixerMeterRms) label = EditorText("inspector.audioMixer.rms", "RMS");
            else if (property == Property::AudioMixerMeterHold) label = EditorText("inspector.audioMixer.heldPeak", "Held peak");
            else label = EditorText("inspector.audioMixer.clippedSamples", "Clipped samples");
            InspectorAudioMixerRow row{.kind = property == Property::AudioMixerMeterPeak || property == Property::AudioMixerMeterRms
                ? Kind::Meter : Kind::ReadOnly, .property = property, .busName = std::string{name}, .label = std::move(label)};
            if (reading == nullptr || !reading->available || !runtime.meters.captureEnabled) {
                row.value = EditorText("inspector.audioMixer.noMeterData", "Unavailable");
            } else {
                std::ostringstream value; value.imbue(std::locale::classic()); value << std::fixed << std::setprecision(3);
                for (std::size_t i = 0U; i < reading->channels && i < reading->channel.size(); ++i) {
                    if (i != 0U) value << " / ";
                    value << (i + 1U) << ": ";
                    const auto& channel = reading->channel[i];
                    row.meterClipped = row.meterClipped || channel.clippedSamples > 0U || channel.heldPeak > 1.0F;
                    if (property == Property::AudioMixerMeterClips) value << channel.clippedSamples;
                    else {
                        const auto level = property == Property::AudioMixerMeterPeak ? channel.peak
                            : property == Property::AudioMixerMeterRms ? channel.rms : channel.heldPeak;
                        row.meterLevel = std::max(row.meterLevel, level); value << level;
                    }
                }
                row.value = value.str();
            }
            add(std::move(row));
        }
        add({.kind = Kind::ReadOnly, .property = Property::AudioMixerMeterFrames, .busName = std::string{name},
            .label = std::string{EditorText("inspector.audioMixer.processedFrames", "Processed frames")},
            .value = reading != nullptr && reading->available && runtime.meters.captureEnabled
                ? std::to_string(reading->processedFrames) : std::string{EditorText("inspector.audioMixer.noMeterData", "Unavailable")}});
    };
    meterRows({}, &runtime.meters.master);
    for (const auto& bus : asset.buses) {
        const auto found = std::find_if(runtime.meters.buses.begin(), runtime.meters.buses.end(), [&](const auto& row) { return row.bus == bus.name; });
        meterRows(bus.name, found == runtime.meters.buses.end() ? nullptr : &*found);
    }
}

std::span<const InspectorAudioMixerRow> InspectorAudioMixerAssetModel::Rows() const noexcept {
    return rows_;
}

std::span<const InspectorAudioMixerRow> InspectorAudioMixerAssetModel::Rows(InspectorSectionId section) const noexcept {
    if (section == InspectorSectionId::AudioMixerBuses) {
        return std::span<const InspectorAudioMixerRow>{ rows_ }.first(busRows_);
    }
    if (section == InspectorSectionId::AudioMixerSnapshots) {
        return std::span<const InspectorAudioMixerRow>{ rows_ }.subspan(busRows_, snapshotRows_);
    }
    if (section == InspectorSectionId::AudioMixerRuntime)
        return std::span<const InspectorAudioMixerRow>{ rows_ }.subspan(busRows_ + snapshotRows_);
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
            text << '\n' << (section == InspectorSectionId::AudioMixerBuses ? "Buses"
                : section == InspectorSectionId::AudioMixerSnapshots ? "Snapshots" : "Runtime") << '\n';
        }
        switch (row.kind) {
        case InspectorAudioMixerRowKind::Group:
            text << '[' << row.label << "]\n";
            break;
        case InspectorAudioMixerRowKind::Text:
        case InspectorAudioMixerRowKind::ReadOnly:
        case InspectorAudioMixerRowKind::Meter:
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

std::string InspectorAudioMixerAssetModel::UniqueEffectId(const kb::audio::AudioMixerBus& bus, kb::audio::AudioDspKind kind) {
    return UniqueName(kb::audio::AudioDspKindName(kind), bus.effects, [](const kb::audio::AudioDspEffect& effect) -> std::string_view { return effect.id; });
}

int InspectorAudioMixerAssetModel::RowHeight(InspectorAudioMixerRowKind kind) noexcept {
    return kind == InspectorAudioMixerRowKind::Group ? kGroupRowHeight : kFieldRowHeight;
}

} // namespace kb::editor
