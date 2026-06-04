#include "rendering/EditorSceneViewportRegionBuilder.hpp"

#if defined(_WIN32)

namespace kb::editor {

HRGN EditorSceneViewportRegionBuilder::BuildCombinedRegion(const RECT& surfaceRect, std::span<const RECT> destinations) noexcept {
    HRGN combinedRegion = CreateRectRgn(0, 0, 0, 0);
    if (combinedRegion == nullptr) {
        return nullptr;
    }

    for (const RECT& destination : destinations) {
        const int left = static_cast<int>(destination.left - surfaceRect.left);
        const int top = static_cast<int>(destination.top - surfaceRect.top);
        const int right = static_cast<int>(destination.right - surfaceRect.left);
        const int bottom = static_cast<int>(destination.bottom - surfaceRect.top);
        HRGN presentRegion = CreateRectRgn(left, top, right, bottom);
        if (presentRegion == nullptr) {
            DeleteObject(combinedRegion);
            return nullptr;
        }
        CombineRgn(combinedRegion, combinedRegion, presentRegion, RGN_OR);
        DeleteObject(presentRegion);
    }

    return combinedRegion;
}

} // namespace kb::editor

#endif
