#pragma once

#include "engine/assets/AssetMetadata.hpp"
#include "engine/assets/AssetHandle.hpp"
#include "inspection/InspectorAudioMixerAssetModel.hpp"
#include "kb/editor/theme/EditorTheme.hpp"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif

#include <optional>

namespace kb::assets {

class AssetManager;

} // namespace kb::assets

namespace kb::editor {

struct InspectorAudioMixerAssetHit {
    InspectorHitKind kind = InspectorHitKind::None;
    InspectorSectionId section = InspectorSectionId::None;
    InspectorPropertyId property = InspectorPropertyId::None;
    int index = -1;
#if defined(_WIN32)
    RECT rect{};
#endif
};

class InspectorAudioMixerAssetView {
public:
    InspectorAudioMixerAssetView() = delete;

    [[nodiscard]] static bool Supports(const kb::assets::AssetMetadata& metadata) noexcept;
    [[nodiscard]] static kb::assets::AssetHandle<kb::audio::AudioMixerAsset> LoadCached(
        kb::assets::AssetManager& manager,
        kb::assets::AssetId id);
    [[nodiscard]] static int ContentHeight(
        const InspectorPanelState& state,
        const kb::audio::AudioMixerAsset& asset);
    [[nodiscard]] static bool IsRowEditing(
        const InspectorPanelState& state,
        kb::assets::AssetId id,
        const InspectorAudioMixerRow& row) noexcept;

#if defined(_WIN32)
    static void Paint(
        HDC dc,
        const RECT& content,
        const EditorTheme& theme,
        const InspectorPanelState& state,
        const kb::assets::AssetMetadata& metadata,
        const kb::audio::AudioMixerAsset& asset);
    [[nodiscard]] static InspectorAudioMixerAssetHit HitTest(
        const RECT& content,
        const InspectorPanelState& state,
        const kb::audio::AudioMixerAsset& asset,
        int x,
        int y);
    [[nodiscard]] static std::optional<RECT> RowBounds(
        const RECT& content,
        const InspectorPanelState& state,
        const kb::audio::AudioMixerAsset& asset,
        int flatIndex);
#endif
};

} // namespace kb::editor
