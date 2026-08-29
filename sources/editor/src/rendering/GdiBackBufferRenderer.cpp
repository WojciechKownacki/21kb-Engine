#include "rendering/GdiBackBufferRenderer.hpp"

#if defined(_WIN32)
#include "rendering/gdi/ScopedPaint.hpp"

#include <algorithm>
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

    [[nodiscard]] HDC Ensure(HDC target, int width, int height, bool& recreated) {
        recreated = false;
        if (dc_ != nullptr && bitmap_ != nullptr && width <= width_ && height <= height_) {
            return dc_;
        }

        // A fresh bitmap holds undefined pixels, so this frame cannot rely on the
        // retained copy and must repaint the whole client.
        recreated = true;
        Reset();
        if (target == nullptr || width <= 0 || height <= 0) {
            return nullptr;
        }

        const int bufferWidth = GrowDimension(width);
        const int bufferHeight = GrowDimension(height);

        dc_ = CreateCompatibleDC(target);
        if (dc_ == nullptr) {
            return nullptr;
        }

        bitmap_ = CreateCompatibleBitmap(target, bufferWidth, bufferHeight);
        if (bitmap_ == nullptr) {
            Reset();
            return nullptr;
        }

        oldBitmap_ = static_cast<HBITMAP>(SelectObject(dc_, bitmap_));
        width_ = bufferWidth;
        height_ = bufferHeight;
        return dc_;
    }

private:
    [[nodiscard]] static int GrowDimension(int value) noexcept {
        constexpr int kGranularity = 256;
        return std::max(kGranularity, ((value + kGranularity - 1) / kGranularity) * kGranularity);
    }

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

    bool bufferRecreated = false;
    HDC memoryDc = BackBufferFor(window).Ensure(targetDc, width, height, bufferRecreated);
    if (memoryDc == nullptr) {
        return;
    }
    const RECT dirtyRect = bufferRecreated ? client : paintRect;

    // Clip the painter to the dirty rect and blit only that region back: the retained buffer
    // already holds the last frame everywhere else. Partial invalidations then cost a partial
    // repaint instead of a full-window workspace redraw.
    const int savedClip = SaveDC(memoryDc);
    IntersectClipRect(memoryDc, dirtyRect.left, dirtyRect.top, dirtyRect.right, dirtyRect.bottom);
    paint(
        GdiBackBufferPaintContext{
            .dc = memoryDc,
            .client = client,
            .dirty = dirtyRect,
            .width = width,
            .height = height,
        },
        context);
    RestoreDC(memoryDc, savedClip);

    BitBlt(targetDc, paintRect.left, paintRect.top, paintWidth, paintHeight, memoryDc, paintRect.left, paintRect.top, SRCCOPY);
}

} // namespace kb::editor

#endif
