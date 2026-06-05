#include "rendering/EditorTabIndicatorPainter.hpp"

#if defined(_WIN32)
namespace kb::editor {

void EditorTabIndicatorPainter::PaintActive(HDC dc, const RECT& tabRect, const EditorTheme& theme) {
    static_cast<void>(dc);
    static_cast<void>(tabRect);
    static_cast<void>(theme);
}

} // namespace kb::editor

#endif
