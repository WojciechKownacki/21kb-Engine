#pragma once

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif

namespace kb::editor {

#if defined(_WIN32)

class ScopedPaint {
public:
    explicit ScopedPaint(HWND window) : window_(window), dc_(BeginPaint(window, &paintStruct_)) {}

    ~ScopedPaint() {
        if (window_ != nullptr) {
            EndPaint(window_, &paintStruct_);
        }
    }

    ScopedPaint(const ScopedPaint&) = delete;
    ScopedPaint& operator=(const ScopedPaint&) = delete;

    [[nodiscard]] HDC Dc() const noexcept {
        return dc_;
    }

    [[nodiscard]] RECT PaintRect() const noexcept {
        return paintStruct_.rcPaint;
    }

private:
    HWND window_ = nullptr;
    PAINTSTRUCT paintStruct_{};
    HDC dc_ = nullptr;
};

#endif

} // namespace kb::editor
