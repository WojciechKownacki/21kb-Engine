#pragma once

#include "scene/StaticMeshEditorSession.hpp"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif

namespace kb::editor {

#if defined(_WIN32)

// The Static Mesh window: the GPU preview in the middle with its view buttons above and its level-of-detail ruler
// below, and the Mesh, Materials and LOD Chain cards on the right. The window draws and hit-tests from the same
// layout pass, so what a click lands on is what was painted.
class StaticMeshEditorPanel final {
public:
    StaticMeshEditorPanel() = delete;

    // Everything but the GPU preview, which presents into ViewportRect().
    static void Paint(HDC dc, const RECT& content, const StaticMeshEditorSession& session);
    [[nodiscard]] static RECT ViewportRect(const RECT& content) noexcept;

    [[nodiscard]] static bool PointerDown(
        const RECT& content, int x, int y, StaticMeshEditorSession& session, kb::assets::AssetManager& manager);
    // A slider being dragged follows the pointer.
    [[nodiscard]] static bool PointerMove(const RECT& content, int x, int y, StaticMeshEditorSession& session);
    [[nodiscard]] static bool PointerUp(StaticMeshEditorSession& session) noexcept;
    // Scrolls the side panel when the pointer is over it.
    [[nodiscard]] static bool Wheel(const RECT& content, int x, int y, float wheelSteps, StaticMeshEditorSession& session);
};

#endif

} // namespace kb::editor
