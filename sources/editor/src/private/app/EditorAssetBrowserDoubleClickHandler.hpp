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
    SkeletalMeshEditorOpened,
    AnimationClipEditorOpened,
    AnimatorEditorOpened,
    ParticleEditorOpened,
};

class EditorAssetBrowserDoubleClickHandler {
public:
    EditorAssetBrowserDoubleClickHandler() = delete;

    [[nodiscard]] static EditorAssetBrowserDoubleClickResult HandleMaterialAssetDoubleClick(
        const kb::assets::AssetMetadata& metadata,
        EditorAssetBrowserState& state,
        kb::assets::AssetManager& manager) {
        return (metadata.type == "RenderMaterial" || metadata.type == "RenderMaterialInstance" || metadata.type == "RenderMaterialGraph") && state.SelectAsset(metadata.id, manager)
            ? EditorAssetBrowserDoubleClickResult::MaterialEditorOpened
            : EditorAssetBrowserDoubleClickResult::None;
    }

    // The single open route shared by double-click and the Open command.
    [[nodiscard]] static EditorAssetBrowserDoubleClickResult OpenAsset(
        HWND owner,
        const kb::assets::AssetMetadata& metadata,
        EditorSceneContext& sceneContext);
    [[nodiscard]] static EditorAssetBrowserDoubleClickResult HandleDoubleClick(HWND owner, const RECT& content, int x, int y, EditorSceneContext& sceneContext);
};

#endif

} // namespace kb::editor
