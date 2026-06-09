#include "rendering/GdiBackBufferRenderer.hpp"

#if defined(_WIN32)
#include "rendering/gdi/ScopedPaint.hpp"

#include <memory>
#include <vector>

namespace kb::editor {
namespace {

class RetainedBackBuffer {
public:
    explicit RetainedBackBuffer(HWND window) noexcept
        : window_(window) {}

    ~RetainedBackBuffer() {
        Reset();
    }

    RetainedBackBuffer(const RetainedBackBuffer&) = delete;
    RetainedBackBuffer& operator=(const RetainedBackBuffer&) = delete;

    [[nodiscard]] HWND Window() const noexcept {
        return window_;
    }

    [[nodiscard]] HDC Ensure(HDC target, int width, int height) {
        if (dc_ != nullptr && bitmap_ != nullptr && width_ == width && height_ == height) {
            return dc_;
        }

        Reset();
        if (target == nullptr || width <= 0 || height <= 0) {
            return nullptr;
        }

        dc_ = CreateCompatibleDC(target);
        if (dc_ == nullptr) {
            return nullptr;
        }

        bitmap_ = CreateCompatibleBitmap(target, width, height);
        if (bitmap_ == nullptr) {
            Reset();
            return nullptr;
        }

        oldBitmap_ = static_cast<HBITMAP>(SelectObject(dc_, bitmap_));
        width_ = width;
        height_ = height;
        return dc_;
    }

private:
    void Reset() noexcept {
        if (dc_ != nullptr) {
            if (oldBitmap_ != nullptr) {
                SelectObject(dc_, oldBitmap_);
                oldBitmap_ = nullptr;
            }
            DeleteDC(dc_);
            dc_ = nullptr;
        }
        if (bitmap_ != nullptr) {
            DeleteObject(bitmap_);
            bitmap_ = nullptr;
        }
        width_ = 0;
        height_ = 0;
    }

    HWND window_ = nullptr;
    HDC dc_ = nullptr;
    HBITMAP bitmap_ = nullptr;
    HBITMAP oldBitmap_ = nullptr;
    int width_ = 0;
    int height_ = 0;
};

[[nodiscard]] RetainedBackBuffer& BackBufferFor(HWND window) {
    static std::vector<std::unique_ptr<RetainedBackBuffer>> buffers;
    std::erase_if(buffers, [](const std::unique_ptr<RetainedBackBuffer>& buffer) {
        return buffer == nullptr || buffer->Window() == nullptr || IsWindow(buffer->Window()) == 0;
    });

    for (const std::unique_ptr<RetainedBackBuffer>& buffer : buffers) {
        if (buffer != nullptr && buffer->Window() == window) {
            return *buffer;
        }
    }

    buffers.push_back(std::make_unique<RetainedBackBuffer>(window));
    return *buffers.back();
}

} // namespace

void GdiBackBufferRenderer::Paint(HWND window, GdiBackBufferPaintFn paint, void* context) {
    if (paint == nullptr) {
        return;
    }

    ScopedPaint paintScope(window);
    HDC targetDc = paintScope.Dc();
    if (targetDc == nullptr) {
        return;
    }

    RECT client{};
    GetClientRect(window, &client);
    const int width = client.right - client.left;
    const int height = client.bottom - client.top;
    if (width <= 0 || height <= 0) {
        return;
    }

    const RECT paintRect = paintScope.PaintRect();
    const int paintWidth = paintRect.right - paintRect.left;
    const int paintHeight = paintRect.bottom - paintRect.top;
    if (paintWidth <= 0 || paintHeight <= 0) {
        return;
    }

    HDC memoryDc = BackBufferFor(window).Ensure(targetDc, width, height);
    if (memoryDc == nullptr) {
        return;
    }

    paint(
        GdiBackBufferPaintContext{
            .dc = memoryDc,
            .client = client,
            .dirty = paintRect,
            .width = width,
            .height = height,
        },
        context);

    BitBlt(targetDc, 0, 0, width, height, memoryDc, 0, 0, SRCCOPY);
}

} // namespace kb::editor

#endif
