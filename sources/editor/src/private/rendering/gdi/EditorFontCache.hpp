#pragma once

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif

namespace kb::editor {

#if defined(_WIN32)

// Creating a GDI font makes Windows map a family, weight and size onto a real face,
// which is far too slow to do per row of a panel - and the panels did exactly that,
// thousands of times a second while a value was being dragged. The editor draws in
// a handful of sizes and weights, so each one is made once and kept.
//
// The handles live for the process on purpose: they are a fixed, tiny set, and every
// panel that draws text wants the same ones.
class EditorFontCache {
public:
    EditorFontCache() = delete;

    [[nodiscard]] static HFONT Acquire(int pointSize, int weight);
};

#endif

} // namespace kb::editor
