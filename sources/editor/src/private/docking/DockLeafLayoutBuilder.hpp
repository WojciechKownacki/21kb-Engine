#pragma once

#include "docking/DockLayoutBuildSettings.hpp"
#include "docking/DockNode.hpp"
#include "kb/editor/docking/DockTypes.hpp"

namespace kb::editor {

class DockLeafLayoutBuilder {
public:
    void Build(const DockNode& node, const DockRect& rect, DockLayout& layout, const DockLayoutBuildSettings& settings) const;
};

} // namespace kb::editor
