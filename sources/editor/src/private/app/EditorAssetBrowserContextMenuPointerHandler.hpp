#pragma once

#include "assets/EditorAssetBrowserHitTester.hpp"

#include <optional>

namespace kb::editor {

#if defined(_WIN32)

class EditorSceneContext;

class EditorAssetBrowserContextMenuPointerHandler {
public:
    EditorAssetBrowserContextMenuPointerHandler() = delete;

    [[nodiscard]] static std::optional<bool> HandleOpenMenuPointerDown(HWND owner, const EditorAssetBrowserHit& hit, EditorSceneContext& sceneContext);
    // window positions the popup over the panel that was clicked; owner is the
    // top-level window that owns any modal a command raises, and is the same window
    // the double-click route uses so the two cannot disagree.
    [[nodiscard]] static bool HandleRightButtonDown(HWND window, HWND owner, const RECT& content, int x, int y,
                                                     EditorSceneContext& sceneContext);
    [[nodiscard]] static bool HandlePointerMove(const RECT& content, int x, int y, EditorSceneContext& sceneContext);
};

#endif

} // namespace kb::editor
