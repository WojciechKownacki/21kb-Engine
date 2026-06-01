#pragma once

#include "kb/render/DisplayConfig.hpp"
#include "kb/render/Renderer.hpp"
#include "kb/render/RenderSurface.hpp"
#include "kb/render/SimpleTriangle.hpp"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif

namespace kb::editor {

class EditorSceneBgfxViewport {
public:
#if defined(_WIN32)
    ~EditorSceneBgfxViewport();

    EditorSceneBgfxViewport(const EditorSceneBgfxViewport&) = delete;
    EditorSceneBgfxViewport& operator=(const EditorSceneBgfxViewport&) = delete;

    EditorSceneBgfxViewport() = default;

    void Configure(HINSTANCE instance, HWND parent) noexcept;
    void Shutdown();
    void BeginPaintLayout() noexcept;
    void BeginPaintLayout(HWND parent) noexcept;
    void QueuePresent(const RECT& rect) noexcept;
    void QueuePresent(HWND parent, const RECT& rect) noexcept;
    void FlushQueuedPresent();
    void Present(const RECT& rect);
    void Present(HWND parent, const RECT& rect);
    void Hide() noexcept;

private:
    class Win32Surface final : public render::RenderSurface {
    public:
        explicit Win32Surface(HWND window) noexcept;

        [[nodiscard]] std::uint32_t Width() const noexcept override;
        [[nodiscard]] std::uint32_t Height() const noexcept override;
        [[nodiscard]] void* NativeWindowHandle() const noexcept override;
        [[nodiscard]] void* NativeDisplayHandle() const noexcept override;

    private:
        HWND window_ = nullptr;
    };

    [[nodiscard]] bool UseParent(HWND parent) noexcept;
    [[nodiscard]] bool EnsureWindow();
    [[nodiscard]] bool EnsureRenderer();
    void MoveTo(const RECT& rect);
    void RenderFrame();

    static LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wparam, LPARAM lparam);

    HINSTANCE instance_ = nullptr;
    HWND defaultParent_ = nullptr;
    HWND parent_ = nullptr;
    HWND window_ = nullptr;
    RECT lastRect_{};
    RECT queuedRect_{};
    HWND paintParent_ = nullptr;
    HWND queuedParent_ = nullptr;
    bool hasRect_ = false;
    bool hasQueuedRect_ = false;
    bool windowClassRegistered_ = false;
    bool triangleReady_ = false;
    render::Renderer renderer_;
    render::SimpleTriangle triangle_;
#endif
};

} // namespace kb::editor
