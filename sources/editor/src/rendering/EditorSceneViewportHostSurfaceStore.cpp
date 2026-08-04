#include "rendering/EditorSceneBgfxViewport.hpp"

#if defined(_WIN32)
#include "rendering/EditorSceneViewportGeometry.hpp"

#include <algorithm>
#include <memory>

namespace kb::editor {
namespace {

[[nodiscard]] std::uint32_t RectWidth(const RECT& rect) noexcept {
    return EditorSceneViewportGeometry::RectWidth(rect);
}

[[nodiscard]] std::uint32_t RectHeight(const RECT& rect) noexcept {
    return EditorSceneViewportGeometry::RectHeight(rect);
}

} // namespace

void EditorSceneBgfxViewport::HostSurfaceStore::Clear() noexcept {
    hostSurfaces_.clear();
}

EditorSceneBgfxViewport::HostSurface* EditorSceneBgfxViewport::HostSurfaceStore::Ensure(HWND host, std::uint64_t key) {
    if (host == nullptr) {
        return nullptr;
    }
    if (HostSurface* existing = Find(host, key); existing != nullptr) {
        return existing;
    }

    std::unique_ptr<HostSurface> surface = std::make_unique<HostSurface>();
    surface->host = host;
    surface->key = key;
    hostSurfaces_.push_back(std::move(surface));
    return hostSurfaces_.back().get();
}

EditorSceneBgfxViewport::HostSurface* EditorSceneBgfxViewport::HostSurfaceStore::Find(HWND host, std::uint64_t key) noexcept {
    if (host == nullptr) {
        return nullptr;
    }

    const auto iter = std::ranges::find_if(hostSurfaces_, [host, key](const std::unique_ptr<HostSurface>& surface) {
        return surface != nullptr && surface->host == host && surface->key == key;
    });
    return iter == hostSurfaces_.end() ? nullptr : iter->get();
}

EditorSceneBgfxViewport::HostSurface* EditorSceneBgfxViewport::HostSurfaceStore::FindByWindow(HWND window) noexcept {
    if (window == nullptr) {
        return nullptr;
    }

    const auto iter = std::ranges::find_if(hostSurfaces_, [window](const std::unique_ptr<HostSurface>& surface) {
        return surface != nullptr && (surface->window == window || surface->clipWindow == window);
    });
    return iter == hostSurfaces_.end() ? nullptr : iter->get();
}

void EditorSceneBgfxViewport::HostSurfaceStore::MarkHostNotPresented(HWND host) noexcept {
    for (const std::unique_ptr<HostSurface>& surface : hostSurfaces_) {
        if (surface != nullptr && surface->host == host) {
            surface->presentedInCurrentPaint = false;
            surface->layoutActiveInCurrentPaint = false;
        }
    }
}

bool EditorSceneBgfxViewport::HostSurfaceStore::HasVisibleUnpresentedForHost(HWND host) const noexcept {
    for (const std::unique_ptr<HostSurface>& surface : hostSurfaces_) {
        if (surface != nullptr &&
            surface->host == host &&
            !surface->layoutActiveInCurrentPaint &&
            surface->clipWindow != nullptr &&
            IsWindow(surface->clipWindow) != 0 &&
            IsWindowVisible(surface->clipWindow) != 0) {
            return true;
        }
    }
    return false;
}

void EditorSceneBgfxViewport::HostSurfaceStore::MarkLayoutActive(HostSurface& surface) noexcept {
    surface.layoutActiveInCurrentPaint = true;
}

void EditorSceneBgfxViewport::HostSurfaceStore::Hide(HostSurface& surface) noexcept {
    // Only repaint the uncovered host area on the visible -> hidden transition.
    // Invalidating every call would busy-loop the editor frame while a non-Scene
    // tab is active (each hide would post a fresh WM_PAINT).
    const bool wasVisible = surface.clipWindow != nullptr && IsWindow(surface.clipWindow) != 0 && IsWindowVisible(surface.clipWindow) != 0;
    if (surface.window != nullptr && IsWindow(surface.window) != 0) {
        ShowWindow(surface.window, SW_HIDE);
    }
    if (surface.clipWindow != nullptr && IsWindow(surface.clipWindow) != 0) {
        ShowWindow(surface.clipWindow, SW_HIDE);
    }
    if (wasVisible && surface.host != nullptr && IsWindow(surface.host) != 0 && RectWidth(surface.rect) > 0U && RectHeight(surface.rect) > 0U) {
        InvalidateRect(surface.host, &surface.rect, FALSE);
    }
    surface.presentedInCurrentPaint = false;
    surface.layoutActiveInCurrentPaint = false;
    surface.layoutBounds = {};
    surface.hasLayoutBounds = false;
}

void EditorSceneBgfxViewport::HostSurfaceStore::HideUnpresentedForHost(HWND host) noexcept {
    for (const std::unique_ptr<HostSurface>& surface : hostSurfaces_) {
        if (surface != nullptr && surface->host == host && !surface->layoutActiveInCurrentPaint) {
            Hide(*surface);
        }
    }
}

void EditorSceneBgfxViewport::HostSurfaceStore::HideForHost(HWND host) noexcept {
    for (const std::unique_ptr<HostSurface>& surface : hostSurfaces_) {
        if (surface != nullptr && surface->host == host) {
            Hide(*surface);
        }
    }
}

void EditorSceneBgfxViewport::HostSurfaceStore::HideAll() noexcept {
    for (const std::unique_ptr<HostSurface>& surface : hostSurfaces_) {
        if (surface != nullptr) {
            Hide(*surface);
        }
    }
}

void EditorSceneBgfxViewport::HostSurfaceStore::ReleaseWindow(HWND window) noexcept {
    HostSurface* surface = FindByWindow(window);
    if (surface == nullptr) {
        return;
    }
    if (surface->window == window) {
        surface->presentTarget.Shutdown();
        surface->window = nullptr;
    }
    if (surface->clipWindow == window) {
        surface->presentTarget.Shutdown();
        surface->clipWindow = nullptr;
        surface->window = nullptr;
    }
    surface->rect = {};
    surface->presentedInCurrentPaint = false;
    surface->layoutActiveInCurrentPaint = false;
    surface->layoutBounds = {};
    surface->hasLayoutBounds = false;
}

void EditorSceneBgfxViewport::HostSurfaceStore::ShutdownPresentTargets() noexcept {
    for (const std::unique_ptr<HostSurface>& surface : hostSurfaces_) {
        if (surface != nullptr) {
            surface->presentTarget.Shutdown();
        }
    }
}

void EditorSceneBgfxViewport::HostSurfaceStore::DestroyWindows() noexcept {
    for (const std::unique_ptr<HostSurface>& surface : hostSurfaces_) {
        if (surface == nullptr) {
            continue;
        }
        if (surface->window != nullptr && IsWindow(surface->window) != 0) {
            const HWND window = surface->window;
            surface->window = nullptr;
            DestroyWindow(window);
        } else {
            surface->window = nullptr;
        }
        if (surface->clipWindow != nullptr && IsWindow(surface->clipWindow) != 0) {
            const HWND window = surface->clipWindow;
            surface->clipWindow = nullptr;
            DestroyWindow(window);
        } else {
            surface->clipWindow = nullptr;
        }
        surface->rect = {};
        surface->presentedInCurrentPaint = false;
        surface->layoutActiveInCurrentPaint = false;
        surface->layoutBounds = {};
        surface->hasLayoutBounds = false;
    }
}

void EditorSceneBgfxViewport::HostSurfaceStore::ShowPresentedWindows() noexcept {
    for (const std::unique_ptr<HostSurface>& surface : hostSurfaces_) {
        if (surface == nullptr ||
            !surface->presentedInCurrentPaint ||
            surface->clipWindow == nullptr ||
            surface->window == nullptr ||
            IsWindow(surface->clipWindow) == 0 ||
            IsWindow(surface->window) == 0) {
            continue;
        }

        UINT flags = SWP_NOACTIVATE | SWP_NOOWNERZORDER | SWP_NOREDRAW | SWP_SHOWWINDOW;
        if (!EditorSceneBgfxViewport::ShouldPreserveHostSurfaceBits(surface->key)) {
            flags |= SWP_NOCOPYBITS;
        }
        if (IsWindowVisible(surface->clipWindow) == 0) {
            SetWindowPos(
                surface->clipWindow,
                HWND_BOTTOM,
                surface->rect.left,
                surface->rect.top,
                static_cast<int>(RectWidth(surface->rect)),
                static_cast<int>(RectHeight(surface->rect)),
                flags);
        }
        if (IsWindowVisible(surface->window) == 0) {
            SetWindowPos(
                surface->window,
                HWND_TOP,
                0,
                0,
                static_cast<int>(RectWidth(surface->rect)),
                static_cast<int>(RectHeight(surface->rect)),
                flags);
        }
    }
}

} // namespace kb::editor

#endif
