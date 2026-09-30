#pragma once

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif

#include "app/EditorEditCommandPolicy.hpp"

namespace kb::editor {

class EditorSceneContext;

class EditorEditCommandInputHandler {
public:
    explicit EditorEditCommandInputHandler(EditorSceneContext& sceneContext) noexcept;

#if defined(_WIN32)
    [[nodiscard]] bool HandleKeyDown(WPARAM key) const;
#endif
    // Runs an edit command the way its keyboard shortcut does, including committing an open text edit
    // before Save.
    [[nodiscard]] bool ExecuteShortcut(EditorEditCommand command) const;

private:
    EditorSceneContext& sceneContext_;
};

} // namespace kb::editor
