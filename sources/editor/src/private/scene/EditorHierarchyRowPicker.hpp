#pragma once

#include "scene/EditorSceneContext.hpp"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif

namespace kb::editor {

class EditorHierarchyRowPicker {
public:
#if defined(_WIN32)
    [[nodiscard]] static bool SelectAtContentPoint(const RECT& content, int x, int y, EditorSceneContext& sceneContext);
    [[nodiscard]] static kb::scene::SceneEntity EntityAtContentPoint(const RECT& content, int x, int y, const EditorSceneContext& sceneContext);
#endif
};

} // namespace kb::editor
