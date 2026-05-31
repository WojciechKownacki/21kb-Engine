#pragma once

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif

namespace kb::editor {

#if defined(_WIN32)

class ProjectFilesTileTextRenderer {
public:
    ProjectFilesTileTextRenderer() = delete;

    static void PaintWrapped(HDC dc, RECT rect, const char* text, COLORREF color, int pointSize, int weight = FW_NORMAL);
};

#endif

} // namespace kb::editor
