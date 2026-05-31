#pragma once

#include "assets/EditorAssetBrowserHitTester.hpp"

#include <optional>

namespace kb::editor {

#if defined(_WIN32)

class EditorAssetBrowserContentHitTester {
public:
    EditorAssetBrowserContentHitTester() = delete;

    [[nodiscard]] static std::optional<EditorAssetBrowserHit> HitTest(
        const EditorAssetBrowserLayoutRects& layout,
        int x,
        int y,
        const EditorAssetBrowserState& state,
        const kb::assets::AssetManager& manager);
};

#endif

} // namespace kb::editor
