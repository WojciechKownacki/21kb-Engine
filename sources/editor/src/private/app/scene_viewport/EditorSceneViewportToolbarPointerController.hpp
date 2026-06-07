#pragma once

namespace kb::editor {

struct EditorResolvedPanelContent;
class EditorSceneBgfxViewport;
class EditorSceneContext;

class EditorSceneViewportToolbarPointerController {
public:
    EditorSceneViewportToolbarPointerController(EditorSceneContext& sceneContext, EditorSceneBgfxViewport& sceneViewport) noexcept;

    [[nodiscard]] bool HandlePointerDown(const EditorResolvedPanelContent& panelContent, int x, int y);

private:
    EditorSceneContext& sceneContext_;
    EditorSceneBgfxViewport& sceneViewport_;
};

} // namespace kb::editor
