#pragma once

#include "kb/editor/assets/EditorAssetBrowserTypes.hpp"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif

namespace kb::editor {

class EditorSceneContext;

class EditorAssetBrowserContextCommandExecutor {
public:
    EditorAssetBrowserContextCommandExecutor() = delete;

    // owner is the window that owns any modal the command has to raise, such as the
    // unsaved-document guard on Open.
    [[nodiscard]] static bool Execute(EditorAssetContextCommand command, EditorSceneContext& sceneContext, HWND owner = nullptr);
};

} // namespace kb::editor
