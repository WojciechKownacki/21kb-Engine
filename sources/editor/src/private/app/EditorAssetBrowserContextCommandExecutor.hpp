#pragma once

#include "kb/editor/assets/EditorAssetBrowserTypes.hpp"

namespace kb::editor {

class EditorSceneContext;

class EditorAssetBrowserContextCommandExecutor {
public:
    EditorAssetBrowserContextCommandExecutor() = delete;

    [[nodiscard]] static bool Execute(EditorAssetContextCommand command, EditorSceneContext& sceneContext);
};

} // namespace kb::editor
