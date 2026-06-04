#pragma once

#include "scene/EditorViewportPreviewState.hpp"

#include <cstdint>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif

namespace kb::editor {

class EditorSceneViewportGeometry final {
public:
#if defined(_WIN32)
    [[nodiscard]] static std::uint32_t RectWidth(const RECT& rect) noexcept;
    [[nodiscard]] static std::uint32_t RectHeight(const RECT& rect) noexcept;
    [[nodiscard]] static RECT CenteredRectFor(const RECT& bounds, std::uint32_t renderWidth, std::uint32_t renderHeight, EditorViewportFitMode fitMode) noexcept;
    [[nodiscard]] static RECT ClipRectToClient(HWND parent, const RECT& rect) noexcept;
#endif
};

} // namespace kb::editor
