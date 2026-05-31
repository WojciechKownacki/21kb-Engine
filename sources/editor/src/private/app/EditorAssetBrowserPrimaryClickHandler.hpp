#pragma once

#include "assets/EditorAssetBrowserHitTester.hpp"

namespace kb::editor {

#if defined(_WIN32)

class EditorSceneContext;

class EditorAssetBrowserPrimaryClickHandler {
public:
    EditorAssetBrowserPrimaryClickHandler() = delete;

    [[nodiscard]] static bool HandlePointerDown(
        const RECT& content,
        const EditorAssetBrowserHit& hit,
        int x,
        int y,
        EditorSceneContext& sceneContext);
};

#endif

} // namespace kb::editor
