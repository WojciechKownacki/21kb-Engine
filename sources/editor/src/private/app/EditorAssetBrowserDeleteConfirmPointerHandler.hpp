#pragma once

#include "assets/EditorAssetBrowserHitTester.hpp"

namespace kb::editor {

#if defined(_WIN32)

class EditorSceneContext;

class EditorAssetBrowserDeleteConfirmPointerHandler {
public:
    EditorAssetBrowserDeleteConfirmPointerHandler() = delete;

    [[nodiscard]] static bool HandlePointerDown(const RECT& bounds, const EditorAssetBrowserHit& hit, int x, int y, EditorSceneContext& sceneContext);
    [[nodiscard]] static bool HandlePointerMove(const RECT& bounds, int x, int y, EditorSceneContext& sceneContext);
    [[nodiscard]] static bool HandlePointerUp(EditorSceneContext& sceneContext) noexcept;
};

#endif

} // namespace kb::editor
