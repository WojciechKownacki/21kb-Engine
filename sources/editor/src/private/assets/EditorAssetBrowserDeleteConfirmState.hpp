#pragma once

namespace kb::editor {

class EditorAssetBrowserDeleteConfirmState {
public:
    [[nodiscard]] bool IsOpen() const noexcept;
    [[nodiscard]] bool IsDragging() const noexcept;
    [[nodiscard]] int OffsetX() const noexcept;
    [[nodiscard]] int OffsetY() const noexcept;

    void Open() noexcept;
    void Close() noexcept;
    void BeginDrag(int x, int y) noexcept;
    void Drag(int x, int y) noexcept;
    void EndDrag() noexcept;

private:
    bool open_ = false;
    bool dragging_ = false;
    int offsetX_ = 0;
    int offsetY_ = 0;
    int dragStartX_ = 0;
    int dragStartY_ = 0;
    int dragStartOffsetX_ = 0;
    int dragStartOffsetY_ = 0;
};

} // namespace kb::editor
