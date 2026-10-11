#pragma once

#include "platform/win32/EditorSceneDirtyPrompt.hpp"
#include "scene/EditorSceneContext.hpp"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif

#include <optional>

namespace kb::editor {

// Why prefab edit mode is being left.
enum class EditorPrefabLeaveReason {
    CloseEditor,
    ClosePrefab,
};

#if defined(_WIN32)

class EditorSceneLifecycleGuard {
public:
    EditorSceneLifecycleGuard() = delete;

    [[nodiscard]] static std::optional<EditorDirtySceneResolution> ConfirmDirtySceneTransition(
        HWND owner,
        EditorSceneContext& sceneContext,
        EditorSceneTransition transition);
    // Leaves prefab edit mode, first asking what to do with unsaved prefab edits. False when cancelled.
    [[nodiscard]] static bool LeavePrefabEditMode(
        HWND owner,
        EditorSceneContext& sceneContext,
        EditorPrefabLeaveReason reason);
};

#endif

} // namespace kb::editor
