#pragma once

#include "assets/EditorAssetBrowserState.hpp"
#include "engine/assets/AssetManager.hpp"
#include "engine/assets/AssetMetadata.hpp"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif

namespace kb::editor {

#if defined(_WIN32)

class EditorSceneContext;

enum class EditorAssetBrowserDoubleClickResult {
    None,
    BrowserNavigation,
    SceneOpened,
    ScriptEditorOpened,
    MaterialEditorOpened,
};

class EditorAssetBrowserDoubleClickHandler {
public:
    EditorAssetBrowserDoubleClickHandler() = delete;

    [[nodiscard]] static EditorAssetBrowserDoubleClickResult HandleMaterialAssetDoubleClick(
        const kb::assets::AssetMetadata& metadata,
        EditorAssetBrowserState& state,
        kb::assets::AssetManager& manager) {
        return metadata.type == "RenderMaterial" && state.SelectAsset(metadata.id, manager)
            ? EditorAssetBrowserDoubleClickResult::MaterialEditorOpened
            : EditorAssetBrowserDoubleClickResult::None;
    }

    [[nodiscard]] static EditorAssetBrowserDoubleClickResult HandleDoubleClick(HWND owner, const RECT& content, int x, int y, EditorSceneContext& sceneContext);
};

#endif

} // namespace kb::editor
