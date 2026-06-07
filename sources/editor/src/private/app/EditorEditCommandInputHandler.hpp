#pragma once

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif

namespace kb::editor {

class EditorSceneContext;

class EditorEditCommandInputHandler {
public:
    explicit EditorEditCommandInputHandler(EditorSceneContext& sceneContext) noexcept;

#if defined(_WIN32)
    [[nodiscard]] bool HandleKeyDown(WPARAM key) const;
#endif

private:
    EditorSceneContext& sceneContext_;
};

} // namespace kb::editor
