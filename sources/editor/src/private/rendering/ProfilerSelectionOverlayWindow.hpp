#pragma once

#include "kb/editor/theme/EditorTheme.hpp"
#include "rendering/EditorOverlayPopupWindow.hpp"
#include "rendering/EditorSceneViewportTextOverlay.hpp"
#include "scene/EditorSceneContext.hpp"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif

namespace kb::editor {

class EditorSceneBgfxViewport;

// The "What to show" window: the areas to measure, the counters for the panel and the scene, how
// the scene shows them, with a preview of the scene. Every click is stored in the editor
// configuration and reaches the scene at once.
class ProfilerSelectionOverlayWindow final
#if defined(_WIN32)
    : private EditorOverlayPopupWindow::Client
#endif
{
public:
#if defined(_WIN32)
    ProfilerSelectionOverlayWindow() noexcept;
    ~ProfilerSelectionOverlayWindow() = default;

    ProfilerSelectionOverlayWindow(const ProfilerSelectionOverlayWindow&) = delete;
    ProfilerSelectionOverlayWindow& operator=(const ProfilerSelectionOverlayWindow&) = delete;

    // Shows the window over `parent` while the profiler state says it is open, hides it otherwise.
    void Show(HWND parent, const EditorTheme& theme, EditorSceneContext& sceneContext, EditorSceneBgfxViewport& sceneViewport);
    void Hide() noexcept;
    // Paints the window as it is shown, for a capture; nothing is created or shown.
    void PaintInto(HDC dc, const RECT& client, const EditorTheme& theme, EditorSceneContext& sceneContext);
    // A click at (x, y) of a window painted at `client`; whether it hit something.
    bool ClickAt(const RECT& client, int x, int y, const EditorTheme& theme, EditorSceneContext& sceneContext,
        EditorSceneBgfxViewport& sceneViewport);
#endif

private:
#if defined(_WIN32)
    struct Item;
    struct Layout;

    [[nodiscard]] RECT ResolveScreenBounds() const noexcept;
    [[nodiscard]] Layout BuildLayout(const RECT& client) const;
    [[nodiscard]] int ItemAt(const Layout& layout, int x, int y) const;
    void PaintOverlay(HDC dc, const RECT& client) override;
    bool HandleOverlayMessage(UINT message, WPARAM wparam, LPARAM lparam) override;
    // A press at `index` of the layout (-1: on nothing): it picks a choice of an open list, closes the
    // list, or acts on what it hit. Returns whether the window took the press.
    bool Press(const Layout& layout, int index);
    void Activate(const Item& item);
    // Stores the new choice, so the window, the scene and the next session all show it.
    void Commit(const ProfilerSelection& selection);

    EditorOverlayPopupWindow popup_;
    EditorSceneViewportTextOverlay previewRenderer_;
    EditorTheme theme_{};
    EditorSceneContext* sceneContext_ = nullptr;
    EditorSceneBgfxViewport* sceneViewport_ = nullptr;
    int hovered_ = -1;
#endif
};

} // namespace kb::editor
