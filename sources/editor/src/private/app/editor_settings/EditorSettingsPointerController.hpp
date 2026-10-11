#pragma once

#include "rendering/EditorSettingsPanelRenderer.hpp"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif

namespace kb::editor {

class EditorSceneContext;
class EditorDockModel;
class EditorFloatingWindowManager;

class EditorSettingsPointerController final {
public:
    EditorSettingsPointerController(EditorSceneContext& sceneContext, EditorDockModel& dockModel,
        EditorFloatingWindowManager& floatingWindows) noexcept
        : sceneContext_(sceneContext), dockModel_(dockModel), floatingWindows_(floatingWindows) {}
#if defined(_WIN32)
    // `owner` is the window the Language list opens over.
    [[nodiscard]] bool HandlePointerDown(
        HWND owner,
        const RECT& content,
        int x,
        int y);
#endif

private:
    EditorSceneContext& sceneContext_;
    EditorDockModel& dockModel_;
    EditorFloatingWindowManager& floatingWindows_;
};

} // namespace kb::editor
