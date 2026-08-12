#pragma once

#include "engine/audio/AudioMixerAsset.hpp"
#include "inspection/InspectorPanelState.hpp"

#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace kb::editor {

enum class InspectorAudioMixerRowKind : std::uint8_t {
    Group,
    Text,
    Bool,
    ReadOnly,
    Action,
    Empty,
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
    std::string label;
    std::string value;
    bool boolValue = false;
    InspectorDynamicRowIdentity identity;
};

class InspectorAudioMixerAssetModel {
public:
    explicit InspectorAudioMixerAssetModel(const kb::audio::AudioMixerAsset& asset);

    [[nodiscard]] std::span<const InspectorAudioMixerRow> Rows() const noexcept;
    [[nodiscard]] std::span<const InspectorAudioMixerRow> Rows(InspectorSectionId section) const noexcept;
    [[nodiscard]] const InspectorAudioMixerRow* Find(int flatIndex) const noexcept;
    [[nodiscard]] const InspectorAudioMixerRow* Find(
        int flatIndex,
        const InspectorDynamicRowIdentity& identity) const noexcept;
    [[nodiscard]] std::string Text() const;

    [[nodiscard]] static std::string UniqueBusName(const kb::audio::AudioMixerAsset& asset);
    [[nodiscard]] static std::string UniqueSnapshotName(const kb::audio::AudioMixerAsset& asset);
    [[nodiscard]] static int RowHeight(InspectorAudioMixerRowKind kind) noexcept;

private:
    std::vector<InspectorAudioMixerRow> rows_;
    std::size_t busRows_ = 0U;
};

} // namespace kb::editor
