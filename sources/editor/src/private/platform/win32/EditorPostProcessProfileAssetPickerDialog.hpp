#pragma once

#include "engine/assets/AssetId.hpp"
#include "engine/assets/AssetMetadata.hpp"
#include "kb/editor/theme/EditorTheme.hpp"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif

namespace kb::editor {

class EditorSceneContext;

// The picker of the scene's post-process profile (the Inspector's Post Processing > Scene Profile).
class EditorPostProcessProfileAssetPickerDialog {
public:
    EditorPostProcessProfileAssetPickerDialog() = delete;

    [[nodiscard]] static bool MatchesFilter(const kb::assets::AssetMetadata& metadata) noexcept;

#if defined(_WIN32)
    struct Result {
        bool accepted = false;
        kb::assets::AssetId assetId{};
    };

    [[nodiscard]] static Result Show(
        HWND owner,
        const EditorTheme& theme,
        const EditorSceneContext& sceneContext,
        kb::assets::AssetId currentProfile);
#endif
};

} // namespace kb::editor
