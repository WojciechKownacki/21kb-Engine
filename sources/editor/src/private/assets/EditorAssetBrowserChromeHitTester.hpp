#pragma once

#include "assets/EditorAssetBrowserHitTester.hpp"

#include <optional>

namespace kb::editor {

#if defined(_WIN32)

class EditorAssetBrowserChromeHitTester {
public:
    EditorAssetBrowserChromeHitTester() = delete;

    [[nodiscard]] static std::optional<EditorAssetBrowserHit> HitTest(
        const EditorAssetBrowserLayoutRects& layout,
        int x,
        int y,
        const EditorAssetBrowserState& state);
};

#endif

} // namespace kb::editor
