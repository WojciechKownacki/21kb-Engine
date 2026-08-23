#pragma once

#include "assets/EditorAssetBrowserHitTester.hpp"

#include <optional>

namespace kb::editor {

#if defined(_WIN32)

class EditorSceneContext;

class EditorAssetBrowserContextMenuPointerHandler {
public:
    EditorAssetBrowserContextMenuPointerHandler() = delete;

    [[nodiscard]] static std::optional<bool> HandleOpenMenuPointerDown(const EditorAssetBrowserHit& hit, EditorSceneContext& sceneContext);
    [[nodiscard]] static bool HandleRightButtonDown(HWND window, const RECT& content, int x, int y,
                                                     EditorSceneContext& sceneContext);
    [[nodiscard]] static bool HandlePointerMove(const RECT& content, int x, int y, EditorSceneContext& sceneContext);
};

#endif

} // namespace kb::editor
