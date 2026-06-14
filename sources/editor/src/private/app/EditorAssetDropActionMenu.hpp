#pragma once

#include "assets/EditorAssetBrowserHitTester.hpp"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif

namespace kb::editor {

class EditorAssetDropActionMenu {
public:
    EditorAssetDropActionMenu() = delete;

#if defined(_WIN32)
    [[nodiscard]] static EditorAssetDropAction Show(HWND window, int x, int y);
#endif
};

} // namespace kb::editor
