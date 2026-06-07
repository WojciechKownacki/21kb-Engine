#pragma once

#include "kb/editor/theme/EditorTheme.hpp"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif

namespace kb::assets {

class AssetManager;

} // namespace kb::assets

namespace kb::editor {

#if defined(_WIN32)

class EditorAssetBrowserState;

class ProjectFilesOverlayRenderer {
public:
    ProjectFilesOverlayRenderer() = delete;

    static void Paint(
        HDC dc,
        const RECT& content,
        const RECT& overlayBounds,
        const EditorTheme& theme,
        const EditorAssetBrowserState& state,
        const kb::assets::AssetManager& manager);

    [[nodiscard]] static int DeleteConfirmMaxScroll(const RECT& bounds, const EditorAssetBrowserState& state, const kb::assets::AssetManager& manager);
    [[nodiscard]] static RECT DeleteConfirmListRect(const RECT& bounds, const EditorAssetBrowserState& state) noexcept;
    [[nodiscard]] static RECT DeleteConfirmListViewportRect(const RECT& bounds, const EditorAssetBrowserState& state) noexcept;
    [[nodiscard]] static RECT DeleteConfirmListScrollbarTrackRect(const RECT& bounds, const EditorAssetBrowserState& state) noexcept;
    [[nodiscard]] static RECT DeleteConfirmListScrollbarThumbRect(const RECT& bounds, const EditorAssetBrowserState& state, const kb::assets::AssetManager& manager);
    [[nodiscard]] static int DeleteConfirmListRowHeight() noexcept;

    static void PaintDeleteConfirm(
        HDC dc,
        const RECT& bounds,
        const EditorTheme& theme,
        const EditorAssetBrowserState& state,
        const kb::assets::AssetManager& manager);

    static void PaintDeleteConfirmDialogOnly(
        HDC dc,
        const RECT& bounds,
        const EditorTheme& theme,
        const EditorAssetBrowserState& state,
        const kb::assets::AssetManager& manager);

    static void PaintDeleteConfirmDialogAt(
        HDC dc,
        const RECT& dialog,
        const EditorTheme& theme,
        const EditorAssetBrowserState& state,
        const kb::assets::AssetManager& manager);
};

#endif

} // namespace kb::editor
