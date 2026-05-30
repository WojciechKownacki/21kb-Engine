#include "rendering/EditorGdiRenderer.hpp"

#if defined(_WIN32)
#include "rendering/DockWorkspaceRenderer.hpp"
#include "rendering/FloatingEditorWindowRenderer.hpp"
#include "rendering/GdiDrawing.hpp"

namespace kb::editor {

void EditorGdiRenderer::Paint(HWND window, const EditorDockModel& dockModel, const EditorTheme& theme, const EditorMetrics& metrics, const EditorSceneContext& sceneContext, const DockDropPreview* preview) const {
    PAINTSTRUCT paint{};
    HDC targetDc = BeginPaint(window, &paint);

    RECT client{};
    GetClientRect(window, &client);
    const int width = client.right - client.left;
    const int height = client.bottom - client.top;

    ScopedCompatibleDc memoryDc(targetDc);
    ScopedBitmap backBuffer(targetDc, width, height);
    HBITMAP oldBitmap = static_cast<HBITMAP>(SelectObject(memoryDc.handle, backBuffer.handle));
    HDC dc = memoryDc.handle;

    GdiDrawing::FillRectColor(dc, client, GdiDrawing::ToColorRef(theme.background));
    SetBkMode(dc, TRANSPARENT);

    DockWorkspaceRenderer{}.Paint(dc, width, height, dockModel, theme, metrics, sceneContext, preview);
    BitBlt(targetDc, 0, 0, width, height, dc, 0, 0, SRCCOPY);
    SelectObject(memoryDc.handle, oldBitmap);
    EndPaint(window, &paint);
}

void EditorGdiRenderer::PaintFloating(HWND window, const DockPanel& panel, const EditorTheme& theme, const EditorMetrics& metrics, const EditorSceneContext& sceneContext) const {
    PAINTSTRUCT paint{};
    HDC targetDc = BeginPaint(window, &paint);

    RECT client{};
    GetClientRect(window, &client);
    const int width = client.right - client.left;
    const int height = client.bottom - client.top;

    ScopedCompatibleDc memoryDc(targetDc);
    ScopedBitmap backBuffer(targetDc, width, height);
    HBITMAP oldBitmap = static_cast<HBITMAP>(SelectObject(memoryDc.handle, backBuffer.handle));
    HDC dc = memoryDc.handle;

    GdiDrawing::FillRectColor(dc, client, GdiDrawing::ToColorRef(theme.background));
    SetBkMode(dc, TRANSPARENT);

    FloatingEditorWindowRenderer{}.Paint(dc, client, panel, theme, metrics, sceneContext);
    BitBlt(targetDc, 0, 0, width, height, dc, 0, 0, SRCCOPY);
    SelectObject(memoryDc.handle, oldBitmap);
    EndPaint(window, &paint);
}

} // namespace kb::editor

#endif
