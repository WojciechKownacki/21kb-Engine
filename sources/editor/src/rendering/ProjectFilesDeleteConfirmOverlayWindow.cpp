#include "rendering/ProjectFilesDeleteConfirmOverlayWindow.hpp"

#if defined(_WIN32)
#include "assets/EditorAssetBrowserGeometry.hpp"
#include "engine/scene/SceneAssets.hpp"
#include "rendering/GdiDrawing.hpp"
#include "rendering/ProjectFilesOverlayRenderer.hpp"

namespace kb::editor {
namespace {

constexpr wchar_t kDeleteConfirmOverlayClassName[] = L"KBEditorProjectFilesDeleteConfirmOverlay";

} // namespace

ProjectFilesDeleteConfirmOverlayWindow::ProjectFilesDeleteConfirmOverlayWindow() noexcept
    : popup_(kDeleteConfirmOverlayClassName, *this) {}

void ProjectFilesDeleteConfirmOverlayWindow::Show(HWND parent, const EditorTheme& theme, const EditorSceneContext& sceneContext) {
    if (parent == nullptr || !sceneContext.AssetBrowser().IsDeleteConfirmOpen() || !popup_.Ensure(parent)) {
        Hide();
        return;
    }

    theme_ = theme;
    sceneContext_ = &sceneContext;
    if (MoveToCurrentBounds()) {
        popup_.Invalidate();
    }
}

void ProjectFilesDeleteConfirmOverlayWindow::Hide() noexcept {
    popup_.Hide();
}

RECT ProjectFilesDeleteConfirmOverlayWindow::ResolveScreenBounds() const noexcept {
    const HWND owner = popup_.Owner();
    if (owner == nullptr || sceneContext_ == nullptr || IsWindow(owner) == 0) {
        return {};
    }

    RECT client{};
    GetClientRect(owner, &client);
    const RECT dialog = EditorAssetBrowserGeometry::DeleteConfirmRect(
        client,
        sceneContext_->AssetBrowser().DeleteConfirmOffsetX(),
        sceneContext_->AssetBrowser().DeleteConfirmOffsetY());
    return EditorOverlayPopupWindow::OwnerClientToScreen(owner, dialog);
}

bool ProjectFilesDeleteConfirmOverlayWindow::MoveToCurrentBounds() noexcept {
    return popup_.ShowAt(ResolveScreenBounds(), HWND_TOPMOST);
}

void ProjectFilesDeleteConfirmOverlayWindow::PaintOverlay(HDC dc, const RECT& client) {
    if (sceneContext_ == nullptr) {
        return;
    }

    GdiDrawing::FillRectColor(dc, client, RGB(18, 20, 24));
    ProjectFilesOverlayRenderer::PaintDeleteConfirmDialogAt(
        dc,
        client,
        theme_,
        sceneContext_->AssetBrowser(),
        sceneContext_->Scene().Assets().Manager());
}

void ProjectFilesDeleteConfirmOverlayWindow::ForwardMouseWheel(WPARAM wparam, LPARAM lparam) const {
    const HWND owner = popup_.Owner();
    if (owner == nullptr || IsWindow(owner) == 0) {
        return;
    }
    SendMessageW(owner, WM_MOUSEWHEEL, wparam, lparam);
}

ProjectFilesDeleteConfirmOverlayWindow::StateSnapshot ProjectFilesDeleteConfirmOverlayWindow::SnapshotState() const noexcept {
    if (sceneContext_ == nullptr) {
        return {};
    }
    const EditorAssetBrowserState& state = sceneContext_->AssetBrowser();
    return StateSnapshot{
        .open = state.IsDeleteConfirmOpen(),
        .offsetX = state.DeleteConfirmOffsetX(),
        .offsetY = state.DeleteConfirmOffsetY(),
        .listScroll = state.DeleteConfirmListScrollOffset(),
    };
}

bool ProjectFilesDeleteConfirmOverlayWindow::SameSnapshot(const StateSnapshot& left, const StateSnapshot& right) noexcept {
    return left.open == right.open
        && left.offsetX == right.offsetX
        && left.offsetY == right.offsetY
        && left.listScroll == right.listScroll;
}

bool ProjectFilesDeleteConfirmOverlayWindow::HandleOverlayMessage(UINT message, WPARAM wparam, LPARAM lparam) {
    switch (message) {
    case WM_LBUTTONDOWN:
    case WM_RBUTTONDOWN:
    case WM_LBUTTONUP:
    case WM_RBUTTONUP:
    case WM_MOUSEMOVE: {
        const bool pointerClick = message != WM_MOUSEMOVE;
        const StateSnapshot before = SnapshotState();
        popup_.ForwardToOwner(message, wparam, lparam);
        const StateSnapshot after = SnapshotState();
        if (sceneContext_ != nullptr && !after.open) {
            Hide();
        } else if (before.offsetX != after.offsetX || before.offsetY != after.offsetY) {
            static_cast<void>(MoveToCurrentBounds());
            if (before.listScroll != after.listScroll) {
                popup_.Invalidate();
            }
        } else if (before.listScroll != after.listScroll || pointerClick) {
            popup_.Invalidate();
        }
        return true;
    }
    case WM_MOUSEWHEEL: {
        const StateSnapshot before = SnapshotState();
        ForwardMouseWheel(wparam, lparam);
        if (const StateSnapshot after = SnapshotState(); !SameSnapshot(before, after)) {
            popup_.Invalidate();
        }
        return true;
    }
    default:
        return false;
    }
}

} // namespace kb::editor

#endif
