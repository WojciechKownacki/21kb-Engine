#pragma once

#include "engine/assets/AssetId.hpp"
#include "engine/assets/AssetMetadata.hpp"
#include "engine/audio/AudioMixerAsset.hpp"
#include "kb/editor/theme/EditorTheme.hpp"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif

namespace kb::editor {

class EditorSceneContext;

class EditorAudioMixerAssetPickerDialog {
public:
    EditorAudioMixerAssetPickerDialog() = delete;

    [[nodiscard]] static bool MatchesFilter(
        const kb::assets::AssetMetadata& metadata) noexcept {
        return metadata.type == kb::audio::kAudioMixerAssetType;
    }

#if defined(_WIN32)
    struct Result {
        bool accepted = false;
        kb::assets::AssetId assetId{};
    };

    [[nodiscard]] static Result Show(
        HWND owner,
        const EditorTheme& theme,
        const EditorSceneContext& sceneContext,
        kb::assets::AssetId currentMixer);
#endif
};

} // namespace kb::editor
