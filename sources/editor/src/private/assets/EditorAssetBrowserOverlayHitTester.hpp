#pragma once

#include "assets/EditorAssetBrowserHitTester.hpp"

#include <optional>

namespace kb::editor {

#if defined(_WIN32)

class EditorAssetBrowserOverlayHitTester {
public:
    EditorAssetBrowserOverlayHitTester() = delete;

    [[nodiscard]] static std::optional<EditorAssetBrowserHit> HitTestDeleteConfirm(
        const RECT& content,
        int x,
        int y,
        const EditorAssetBrowserState& state,
        const kb::assets::AssetManager& manager,
        const RECT* overlayBounds);

    [[nodiscard]] static std::optional<EditorAssetBrowserHit> HitTestContextMenu(
        const RECT& content,
        int x,
        int y,
        const EditorAssetBrowserState& state,
        const kb::assets::AssetManager& manager);

    [[nodiscard]] static std::optional<EditorAssetBrowserHit> HitTestDropActionMenu(
        const RECT& content,
        int x,
        int y,
        const EditorAssetBrowserState& state);
};

#endif

} // namespace kb::editor
