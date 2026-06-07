#pragma once

namespace kb::editor {

class EditorSceneContext;

class EditorProjectFilesTransientUiController {
public:
    explicit EditorProjectFilesTransientUiController(EditorSceneContext& sceneContext) noexcept;

    void CloseTransientUi() noexcept;

private:
    EditorSceneContext& sceneContext_;
};

} // namespace kb::editor
