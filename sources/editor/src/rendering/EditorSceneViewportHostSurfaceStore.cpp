#include "rendering/EditorSceneBgfxViewport.hpp"

#if defined(_WIN32)
#include "diagnostics/EditorLagTrace.hpp"
#include "rendering/EditorSceneViewportGeometry.hpp"

#include <algorithm>
#include <memory>
#include <sstream>

namespace kb::editor {
namespace {

[[nodiscard]] std::uint32_t RectWidth(const RECT& rect) noexcept {
    return EditorSceneViewportGeometry::RectWidth(rect);
}

[[nodiscard]] std::uint32_t RectHeight(const RECT& rect) noexcept {
    return EditorSceneViewportGeometry::RectHeight(rect);
}

// The store owns exactly one piece of state per child HWND: its own WS_VISIBLE bit. IsWindowVisible()
// answers a different question -- it walks the whole ancestor chain and reports 0 whenever the host
// (or the host's parent) is hidden, even though this store already showed the child. Using it as the
// "is it already shown?" test made Show() believe every surface was hidden for as long as the host was
// not itself visible, so ShowPresentedWindows() re-issued SetWindowPos(SWP_SHOWWINDOW) on both child
// windows on every single presented frame and logged a bogus hidden -> shown transition each time.
[[nodiscard]] bool HasVisibleStyle(HWND window) noexcept {
    return window != nullptr && IsWindow(window) != 0 &&
        (GetWindowLongPtrW(window, GWL_STYLE) & WS_VISIBLE) != 0;
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

std::vector<std::uint64_t> EditorSceneBgfxViewport::HostSurfaceStore::KeysForHost(HWND host) const {
    std::vector<std::uint64_t> keys;
    if (host == nullptr) {
        return keys;
    }
    for (const std::unique_ptr<HostSurface>& surface : hostSurfaces_) {
        if (surface != nullptr && surface->host == host) {
            keys.push_back(surface->key);
        }
    }
    return keys;
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
    const bool wasVisible = HasVisibleStyle(surface.clipWindow);
    if (HasVisibleStyle(surface.window)) {
        ShowWindow(surface.window, SW_HIDE);
    }
    surface.textOverlay.Hide();
    if (wasVisible) {
        ShowWindow(surface.clipWindow, SW_HIDE);
    }
    // Keep the native swapchain. Particle Editor and Scene share the center dock leaf, so tab
    // switches hide one surface and show the other every click. Destroying the framebuffer here
    // forced a full D3D swapchain recreate plus a black first frame that lasted seconds. Hidden
    // HWNDs are not shown until ShowPresentedWindows after a real submit of the newly active tab,
    // so the last valid back buffer stays off-screen until that present replaces it.
    if (wasVisible && surface.host != nullptr && IsWindow(surface.host) != 0 && RectWidth(surface.rect) > 0U && RectHeight(surface.rect) > 0U) {
        std::ostringstream detail;
        detail << "action=hide host=0x" << std::hex << reinterpret_cast<std::uintptr_t>(surface.host)
               << std::dec << " key=" << surface.key
               << " targetValid=" << (surface.presentTarget.IsValid() ? 1 : 0)
               << " extent=" << RectWidth(surface.rect) << 'x' << RectHeight(surface.rect);
        diagnostics::EditorLagTrace::Marker("viewport-surface", detail.str());
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
        surface->textOverlay.Destroy();
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
        surface->textOverlay.Destroy();
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

void EditorSceneBgfxViewport::HostSurfaceStore::Show(HostSurface& surface) noexcept {
    if (surface.clipWindow == nullptr || surface.window == nullptr ||
        IsWindow(surface.clipWindow) == 0 || IsWindow(surface.window) == 0) {
        return;
    }
    // Both windows keep their geometry from EnsureHostSurfaceWindow, which runs earlier in the same
    // paint; the coordinates below only matter on the hidden -> shown transition.
    const bool clipHidden = !HasVisibleStyle(surface.clipWindow);
    const bool renderHidden = !HasVisibleStyle(surface.window);
    UINT flags = SWP_NOACTIVATE | SWP_NOOWNERZORDER | SWP_NOREDRAW | SWP_SHOWWINDOW;
    if (!EditorSceneBgfxViewport::ShouldPreserveHostSurfaceBits(surface.key)) {
        flags |= SWP_NOCOPYBITS;
    }
    if (clipHidden) {
        SetWindowPos(
            surface.clipWindow,
            HWND_BOTTOM,
            surface.rect.left,
            surface.rect.top,
            static_cast<int>(RectWidth(surface.rect)),
            static_cast<int>(RectHeight(surface.rect)),
            flags);
    }
    if (renderHidden) {
        SetWindowPos(
            surface.window,
            HWND_TOP,
            0,
            0,
            static_cast<int>(RectWidth(surface.rect)),
            static_cast<int>(RectHeight(surface.rect)),
            flags);
    }
    surface.textOverlay.Show();
    if (clipHidden || renderHidden) {
        std::ostringstream detail;
        detail << "action=show host=0x" << std::hex << reinterpret_cast<std::uintptr_t>(surface.host)
               << std::dec << " key=" << surface.key
               << " targetValid=" << (surface.presentTarget.IsValid() ? 1 : 0)
               << " extent=" << RectWidth(surface.rect) << 'x' << RectHeight(surface.rect);
        diagnostics::EditorLagTrace::Marker("viewport-surface", detail.str());
    }
}

void EditorSceneBgfxViewport::HostSurfaceStore::ShowPresentedWindows() noexcept {
    for (const std::unique_ptr<HostSurface>& surface : hostSurfaces_) {
        if (surface == nullptr || !surface->presentedInCurrentPaint) {
            continue;
        }
        Show(*surface);
    }
}

} // namespace kb::editor

#endif
