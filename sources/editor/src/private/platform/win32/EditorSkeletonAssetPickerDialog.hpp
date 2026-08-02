#pragma once

#include "engine/assets/AssetId.hpp"
#include "kb/editor/theme/EditorTheme.hpp"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif

namespace kb::editor {

class EditorSceneContext;

class EditorSkeletonAssetPickerDialog {
public:
    EditorSkeletonAssetPickerDialog() = delete;

#if defined(_WIN32)
    struct Result {
        bool accepted = false;
        kb::assets::AssetId assetId{};
    };

    [[nodiscard]] static Result Show(
        HWND owner,
        const EditorTheme& theme,
        const EditorSceneContext& sceneContext,
        kb::assets::AssetId currentSkeleton);
#endif
};

} // namespace kb::editor
