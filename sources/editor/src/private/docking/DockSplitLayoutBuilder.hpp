#pragma once

#include "docking/DockLayoutBuildSettings.hpp"
#include "docking/DockNode.hpp"
#include "kb/editor/docking/DockTypes.hpp"

namespace kb::editor {

struct DockSplitLayoutParts {
    DockRect first;
    DockRect splitter;
    DockRect second;
};

class DockSplitLayoutBuilder {
public:
    [[nodiscard]] DockSplitLayoutParts Build(const DockNode& node, const DockRect& rect, const DockLayoutBuildSettings& settings) const;
};

} // namespace kb::editor
