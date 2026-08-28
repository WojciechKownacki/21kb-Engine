#pragma once

#include "docking/DockNode.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace kb::editor {

// The dock tree as text, so a workspace arrangement can be written to the editor's
// settings file and rebuilt on the next launch.
//
// Preorder token stream, space separated:
//   split : 'S' <'H'|'V'> <ratio> <first subtree> <second subtree>
//   leaf  : 'L' <activePanelId> <panelCount> <panelId>...
// Nothing nests in brackets, so parsing is a single left-to-right pass and a
// truncated or reordered stream is rejected rather than half-applied.
class DockLayoutSerializer {
public:
    DockLayoutSerializer() = delete;

    [[nodiscard]] static std::string Serialize(const DockNode* root);

    // Rebuilds the tree. Returns nullptr when the text is malformed, when a leaf
    // would end up empty, or when it names a panel outside knownPanels or names one
    // twice - such a layout is refused rather than half-applied. Panels the text does
    // not mention are left to the caller: they were closed or floating when it was
    // written, and neither belongs in the dock tree.
    [[nodiscard]] static std::unique_ptr<DockNode> Parse(
        std::string_view text,
        const std::vector<std::uint32_t>& knownPanels,
        std::uint32_t& nextNodeId);
};

} // namespace kb::editor
