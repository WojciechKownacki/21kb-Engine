#pragma once

#include "rendering/EditorSettingsPanelRenderer.hpp"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif

namespace kb::editor {

class EditorSceneContext;

class EditorSettingsPointerController final {
public:
    explicit EditorSettingsPointerController(EditorSceneContext& sceneContext) noexcept : sceneContext_(sceneContext) {}
#if defined(_WIN32)
    [[nodiscard]] bool HandlePointerDown(
        const RECT& content,
        int x,
        int y);
#endif

private:
    EditorSceneContext& sceneContext_;
};

} // namespace kb::editor
