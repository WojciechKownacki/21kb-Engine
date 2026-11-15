#pragma once

#include "rendering/EditorPanelContentResolver.hpp"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif

namespace kb::editor {

class EditorSceneBgfxViewport;
class EditorSceneContext;

#if defined(_WIN32)

// A press in the Game View: the resolution button and its list. A choice is kept in the editor settings.
class EditorGameViewPointerController {
public:
    EditorGameViewPointerController(EditorSceneContext& sceneContext, EditorSceneBgfxViewport& sceneViewport) noexcept;

    // True when the press landed on the bar or on the list, which then took it.
    [[nodiscard]] bool HandlePointerDown(const EditorResolvedPanelContent& panel, int x, int y);

private:
    EditorSceneContext& sceneContext_;
    EditorSceneBgfxViewport& sceneViewport_;
};

#endif

} // namespace kb::editor
