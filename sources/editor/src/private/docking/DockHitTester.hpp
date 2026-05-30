#pragma once

#include "kb/editor/docking/DockTypes.hpp"

namespace kb::editor {

class DockHitTester {
public:
    [[nodiscard]] DockHit HitTest(const DockLayout& layout, int x, int y) const;
};

} // namespace kb::editor
