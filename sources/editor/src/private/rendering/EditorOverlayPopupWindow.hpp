#pragma once

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif

namespace kb::editor {

// A popup that has to appear above an editor window's native child surfaces (the
// bgfx viewports are clipped child windows, so anything painted with GDI onto the
// window itself ends up underneath them). The popup is a top-level WS_POPUP owned
// by that window: owned windows always stay above their owner and its children.
// This class keeps the window plumbing in one place - class registration,
// creation, owner hand-over, placement, back-buffered painting and teardown - and
// hands painting and input to the overlay that uses it.
class EditorOverlayPopupWindow {
public:
#if defined(_WIN32)
    class Client {
    public:
        // Paints the whole popup; `client` is the popup's client rectangle.
        virtual void PaintOverlay(HDC dc, const RECT& client) = 0;
        // Input that reached the popup, with mouse positions in popup client
        // coordinates. Returns true when the message was handled.
        virtual bool HandleOverlayMessage(UINT message, WPARAM wparam, LPARAM lparam) = 0;

    protected:
        ~Client() = default;
    };

    EditorOverlayPopupWindow(const wchar_t* className, Client& client) noexcept;
    ~EditorOverlayPopupWindow();

    EditorOverlayPopupWindow(const EditorOverlayPopupWindow&) = delete;
    EditorOverlayPopupWindow& operator=(const EditorOverlayPopupWindow&) = delete;

    // Creates the popup on first use and makes `owner` its owner.
    [[nodiscard]] bool Ensure(HWND owner);
    // Moves the popup to `screenBounds` and shows it without activating it. Returns
    // true when the popup was hidden, moved or resized, i.e. when it needs painting.
    bool ShowAt(const RECT& screenBounds, HWND insertAfter = HWND_TOP) noexcept;
    void Hide() noexcept;
    void Invalidate() const noexcept;
    // Sends a mouse message that reached the popup to its owner, with the position
    // moved into the owner's client coordinates, so the owner's own input handling
    // (hit testing included) decides what the click means.
    void ForwardToOwner(UINT message, WPARAM wparam, LPARAM lparam) const;

    [[nodiscard]] static RECT OwnerClientToScreen(HWND owner, const RECT& rect) noexcept;

    [[nodiscard]] HWND Window() const noexcept;
    [[nodiscard]] HWND Owner() const noexcept;
    [[nodiscard]] bool Shown() const noexcept;
    [[nodiscard]] RECT ClientBounds() const noexcept;
#endif

private:
#if defined(_WIN32)
    static LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wparam, LPARAM lparam);

    const wchar_t* className_ = nullptr;
    Client* client_ = nullptr;
    HWND window_ = nullptr;
    HWND owner_ = nullptr;
    RECT screenBounds_{};
    bool shown_ = false;
#endif
};

} // namespace kb::editor
