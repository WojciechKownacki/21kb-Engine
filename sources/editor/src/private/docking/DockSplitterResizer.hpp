#pragma once

#include "docking/DockNode.hpp"
#include "kb/editor/docking/DockTypes.hpp"

namespace kb::editor {

class DockSplitterResizer {
public:
    DockSplitterResizer() = delete;

    static void Resize(DockNode* root, std::uint32_t nodeId, int mouseX, int mouseY, const DockLayout& layout) noexcept;
};

} // namespace kb::editor
