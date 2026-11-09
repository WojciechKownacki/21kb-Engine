#pragma once

#include <cstdint>

namespace kb::editor {

struct DockLayoutBuildSettings {
    int tabStripHeight = 0;
    int tabMinWidth = 0;
    int tabWidth = 0;
    int splitterSize = 0;
    // A leaf shown collapsed to its tab strip (the bottom drawer on a small window); 0 for none.
    std::uint32_t collapsedLeafId = 0;
};

} // namespace kb::editor
