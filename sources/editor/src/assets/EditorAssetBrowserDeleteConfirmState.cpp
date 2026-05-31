#include "assets/EditorAssetBrowserDeleteConfirmState.hpp"

namespace kb::editor {

bool EditorAssetBrowserDeleteConfirmState::IsOpen() const noexcept {
    return open_;
}

bool EditorAssetBrowserDeleteConfirmState::IsDragging() const noexcept {
    return dragging_;
}

int EditorAssetBrowserDeleteConfirmState::OffsetX() const noexcept {
    return offsetX_;
}

int EditorAssetBrowserDeleteConfirmState::OffsetY() const noexcept {
    return offsetY_;
}

void EditorAssetBrowserDeleteConfirmState::Open() noexcept {
    open_ = true;
    dragging_ = false;
    offsetX_ = 0;
    offsetY_ = 0;
}

void EditorAssetBrowserDeleteConfirmState::Close() noexcept {
    open_ = false;
    dragging_ = false;
}

void EditorAssetBrowserDeleteConfirmState::BeginDrag(int x, int y) noexcept {
    if (!open_) {
        return;
    }
    dragging_ = true;
    dragStartX_ = x;
    dragStartY_ = y;
    dragStartOffsetX_ = offsetX_;
    dragStartOffsetY_ = offsetY_;
}

void EditorAssetBrowserDeleteConfirmState::Drag(int x, int y) noexcept {
    if (!dragging_) {
        return;
    }
    offsetX_ = dragStartOffsetX_ + (x - dragStartX_);
    offsetY_ = dragStartOffsetY_ + (y - dragStartY_);
}

void EditorAssetBrowserDeleteConfirmState::EndDrag() noexcept {
    dragging_ = false;
}

} // namespace kb::editor
