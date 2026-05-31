#pragma once

#include "kb/editor/theme/EditorTheme.hpp"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif

namespace kb::editor {

#if defined(_WIN32)

class ProjectFilesAssetTileFrameRenderer {
public:
    ProjectFilesAssetTileFrameRenderer() = delete;

    static void Paint(HDC dc, RECT tile, const EditorTheme& theme, bool selected, bool focused = false);
};

#endif

} // namespace kb::editor
