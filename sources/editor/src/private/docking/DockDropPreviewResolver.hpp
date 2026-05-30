#pragma once

#include "kb/editor/docking/DockTypes.hpp"

#include <optional>

namespace kb::editor {

class DockDropPreviewResolver {
public:
    [[nodiscard]] std::optional<DockDropPreview> Resolve(const DockLayout& layout, int x, int y) const;
};

} // namespace kb::editor
