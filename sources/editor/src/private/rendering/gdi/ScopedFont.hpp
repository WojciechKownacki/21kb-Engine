#pragma once

#include "rendering/gdi/EditorFontCache.hpp"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif

namespace kb::editor {

#if defined(_WIN32)
namespace detail {

[[nodiscard]] inline const wchar_t* EditorUiFontFamily() {
    return L"Segoe UI";
}

} // namespace detail

// The font a piece of drawing wants, taken from the shared cache. It is not owned
// here: making one costs Windows a face lookup, and the panels ask for the same
// handful over and over.
struct ScopedFont {
    ScopedFont(int pointSize, int weight)
        : handle(EditorFontCache::Acquire(pointSize, weight)) {}

    ScopedFont(const ScopedFont&) = delete;
    ScopedFont& operator=(const ScopedFont&) = delete;

    HFONT handle = nullptr;
};

#endif

} // namespace kb::editor
