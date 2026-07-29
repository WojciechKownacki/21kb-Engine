#pragma once

#include <array>
#include <cstdint>
#include <string_view>

namespace kb::library {

// A frontend/role an API declaration may target.  Authoring and Server are
// intentionally distinct from the three script frontends: an editor-only or
// authoritative-server API must never leak into a gameplay Lua stub or a
// Visual Graph node palette merely because it is implemented in C++.
enum class LibraryApiSurface : std::uint8_t {
    Native = 1U << 0U,
    Lua = 1U << 1U,
    VisualGraph = 1U << 2U,
    Authoring = 1U << 3U,
    Server = 1U << 4U,
};

using LibraryApiSurfaceMask = std::uint8_t;

[[nodiscard]] constexpr LibraryApiSurfaceMask ToMask(LibraryApiSurface surface) noexcept {
    return static_cast<LibraryApiSurfaceMask>(surface);
}

[[nodiscard]] constexpr bool IsAvailableOnSurface(LibraryApiSurfaceMask availability, LibraryApiSurface surface) noexcept {
    return (availability & ToMask(surface)) != 0U;
}

struct LibraryApiSurfaceManifestEntry {
    std::string_view canonicalName;
    LibraryApiSurfaceMask availability = ToMask(LibraryApiSurface::Native);
    std::string_view description;
};

// APIs intentionally outside ScriptFunctionRegistry.  Keeping them here
// makes their absence from Lua stubs and Visual Graph nodes an audited policy,
// rather than an undocumented side effect of their C++ implementation shape.
inline constexpr std::array<LibraryApiSurfaceManifestEntry, 5U> kLibrarySpecialApiSurfaces{{
    { "EntityHandle", ToMask(LibraryApiSurface::Native), "C++ scene identity handle; scripts use World functions instead." },
    { "Signal<Args...>", ToMask(LibraryApiSurface::Native), "Typed in-process observer list whose C++ callback type cannot cross a script boundary." },
    { "SceneTasks.Start", ToMask(LibraryApiSurface::Native), "Starts a callback-backed scene task; scripts may only observe or cancel task handles." },
    { "ScriptAgentProjectFiles.Write", ToMask(LibraryApiSurface::Authoring), "Generates project-local API artifacts and starter files for editor and coding-agent tooling." },
    { "NetworkObjects.AssignOwner", ToMask(LibraryApiSurface::Server), "Changes authority-owned network object ownership; only an authoritative server may perform it." },
}};

} // namespace kb::library
