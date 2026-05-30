#pragma once

#include "kb/editor/theme/EditorTheme.hpp"
#include "windowing/FloatingWindowControlKind.hpp"
#include "windowing/FloatingWindowControlLayout.hpp"

namespace kb::editor {

class FloatingWindowControlHitTester {
public:
    [[nodiscard]] FloatingWindowControlKind HitTest(const EditorMetrics& metrics, int clientWidth, int x, int y) const noexcept {
        for (FloatingWindowControlKind control : FloatingWindowControlLayout::OrderedControls) {
            if (FloatingWindowControlLayout::Rect(metrics, clientWidth, control).Contains(x, y)) {
                return control;
            }
        }
        return FloatingWindowControlKind::None;
    }
};

} // namespace kb::editor
