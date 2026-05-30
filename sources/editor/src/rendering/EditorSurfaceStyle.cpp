#include "rendering/EditorSurfaceStyle.hpp"

#if defined(_WIN32)
#include "rendering/GdiDrawing.hpp"

namespace kb::editor {

COLORREF EditorSurfaceStyle::FillColor(const EditorTheme& theme, EditorSurfaceKind kind) {
    switch (kind) {
    case EditorSurfaceKind::AppBackground:
        return GdiDrawing::ToColorRef(theme.background);
    case EditorSurfaceKind::DockPanel:
        return GdiDrawing::ToColorRef(theme.panel);
    case EditorSurfaceKind::ScenePanel:
        return GdiDrawing::ToColorRef(theme.chrome);
    case EditorSurfaceKind::HeaderStrip:
        return GdiDrawing::ToColorRef(theme.strip);
    case EditorSurfaceKind::ActiveTab:
        return GdiDrawing::ToColorRef(theme.tabActive);
    case EditorSurfaceKind::InactiveTab:
        return GdiDrawing::ToColorRef(theme.tabInactive);
    case EditorSurfaceKind::ToolbarButton:
        return GdiDrawing::ToColorRef(theme.toolbarButton);
    }

    return GdiDrawing::ToColorRef(theme.background);
}

} // namespace kb::editor

#endif
