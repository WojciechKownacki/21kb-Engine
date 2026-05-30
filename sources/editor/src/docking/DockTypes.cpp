#include "kb/editor/docking/DockTypes.hpp"

namespace kb::editor {

bool DockRect::Contains(int px, int py) const noexcept {
    return px >= x && py >= y && px < x + width && py < y + height;
}

bool DockRect::Empty() const noexcept {
    return width <= 0 || height <= 0;
}

} // namespace kb::editor
