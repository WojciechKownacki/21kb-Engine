#pragma once

#include "engine/audio/AudioMixerAsset.hpp"
#include "inspection/InspectorPanelState.hpp"

#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace kb::editor {
struct EditorAudioMixerRuntimeState;

enum class InspectorAudioMixerRowKind : std::uint8_t {
    Group,
    Text,
    Bool,
    ReadOnly,
    Action,
    Empty,
    Meter,
};

struct InspectorAudioMixerRow {
    InspectorAudioMixerRowKind kind = InspectorAudioMixerRowKind::ReadOnly;
    InspectorSectionId section = InspectorSectionId::None;
    InspectorPropertyId property = InspectorPropertyId::None;
    int flatIndex = -1;
    std::size_t busIndex = static_cast<std::size_t>(-1);
    std::size_t snapshotIndex = static_cast<std::size_t>(-1);
    std::size_t overrideIndex = static_cast<std::size_t>(-1);
    std::string busName;
    std::string snapshotName;
    std::string overrideBusName;
    std::string effectId;
    std::string effectField;
    std::size_t effectIndex = 0U;
    kb::audio::AudioDspKind effectKind = kb::audio::AudioDspKind::LowPass;
    std::string label;
    std::string value;
    bool boolValue = false;
    float meterLevel = 0.0F;
    bool meterClipped = false;
    InspectorDynamicRowIdentity identity;
};

class InspectorAudioMixerAssetModel {
public:
    explicit InspectorAudioMixerAssetModel(const kb::audio::AudioMixerAsset& asset,
        const EditorAudioMixerRuntimeState* runtime = nullptr);

    [[nodiscard]] std::span<const InspectorAudioMixerRow> Rows() const noexcept;
    [[nodiscard]] std::span<const InspectorAudioMixerRow> Rows(InspectorSectionId section) const noexcept;
    [[nodiscard]] const InspectorAudioMixerRow* Find(int flatIndex) const noexcept;
    [[nodiscard]] const InspectorAudioMixerRow* Find(
        int flatIndex,
        const InspectorDynamicRowIdentity& identity) const noexcept;
    [[nodiscard]] std::string Text() const;

    [[nodiscard]] static std::string UniqueBusName(const kb::audio::AudioMixerAsset& asset);
    [[nodiscard]] static std::string UniqueSnapshotName(const kb::audio::AudioMixerAsset& asset);
    [[nodiscard]] static std::string UniqueEffectId(const kb::audio::AudioMixerBus& bus, kb::audio::AudioDspKind kind);
    [[nodiscard]] static int RowHeight(InspectorAudioMixerRowKind kind) noexcept;

private:
    void AppendRuntime(const kb::audio::AudioMixerAsset& asset, const EditorAudioMixerRuntimeState& runtime);
    std::vector<InspectorAudioMixerRow> rows_;
    std::size_t busRows_ = 0U;
    std::size_t snapshotRows_ = 0U;
};

} // namespace kb::editor
