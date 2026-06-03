#pragma once

#include "docking/DockPointerDrag.hpp"
#include "kb/editor/theme/EditorTheme.hpp"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif

namespace kb::editor {

class DockPointerDragFactory {
public:
    DockPointerDragFactory() = delete;

#if defined(_WIN32)
    [[nodiscard]] static DockPointerDrag FromDockHit(HWND window, const DockLayout& layout, const DockHit& hit, int x, int y) noexcept;
    [[nodiscard]] static DockPointerDrag FromFloatingWindow(HWND window, std::uint32_t panelId, int x, int y, const EditorMetrics& metrics) noexcept;
#endif

private:
    [[nodiscard]] static DockRect SourceStripForHit(const DockLayout& layout, const DockHit& hit) noexcept;
    [[nodiscard]] static std::uint32_t SourceTabIndexForHit(const DockLayout& layout, const DockHit& hit) noexcept;
};

} // namespace kb::editor
