#pragma once

#include <span>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif

namespace kb::editor {

class EditorSceneViewportRegionBuilder final {
public:
#if defined(_WIN32)
    [[nodiscard]] static HRGN BuildCombinedRegion(const RECT& surfaceRect, std::span<const RECT> destinations) noexcept;
#endif
};

} // namespace kb::editor
