#pragma once

#include "scene/EditorSceneContext.hpp"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif

#include <optional>
#include <string_view>

namespace kb::editor {

#if defined(_WIN32)

class EditorSceneLifecycleGuard {
public:
    EditorSceneLifecycleGuard() = delete;

    [[nodiscard]] static std::optional<EditorDirtySceneResolution> ConfirmDirtySceneTransition(
        HWND owner,
        EditorSceneContext& sceneContext,
        std::wstring_view action);
};

#endif

} // namespace kb::editor
