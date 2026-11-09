#pragma once

#include "kb/editor/docking/DockTypes.hpp"
#include "rendering/EditorSceneBgfxViewport.hpp"
#include "scene/StaticMeshEditorSession.hpp"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif

namespace kb::editor {

#if defined(_WIN32)

// Draws the Static Mesh window's preview into the viewport its panel lays out.
class StaticMeshEditorViewport final {
public:
    StaticMeshEditorViewport() = delete;

    // False when there is nothing to draw: no preview yet, or the host window is not showing.
    [[nodiscard]] static bool Present(
        EditorSceneBgfxViewport& viewport, HWND host, const RECT& content, const DockPanel& panel,
        const StaticMeshEditorSession& session);
};

#endif

} // namespace kb::editor
