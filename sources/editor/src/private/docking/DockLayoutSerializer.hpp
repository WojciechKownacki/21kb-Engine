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
// A floating window that holds several panels follows the tree, as one more leaf:
//   window: 'F' <activePanelId> <panelCount> <panelId>...
// A window with a single panel is left out; the session of that panel places it.
// Nothing nests in brackets, so parsing is a single left-to-right pass and a
// truncated or reordered stream is rejected rather than half-applied.
class DockLayoutSerializer {
public:
    DockLayoutSerializer() = delete;

    [[nodiscard]] static std::string Serialize(const DockNode* root);
    [[nodiscard]] static std::string Serialize(const DockNode* root, const std::vector<DockNode>& floating);

    // Rebuilds the tree. Returns nullptr when the text is malformed, names a panel
    // twice, or holds no panel this build has - such a layout is refused rather than
    // half-applied. A panel outside knownPanels (one a later build removed) is dropped:
    // its tab goes, an emptied group goes with it, and the rest is kept. Panels the text
    // does not mention are left to the caller: they were closed or floating when it was
    // written, and neither belongs in the dock tree.
    [[nodiscard]] static std::unique_ptr<DockNode> Parse(
        std::string_view text,
        const std::vector<std::uint32_t>& knownPanels,
        std::uint32_t& nextNodeId);
    // The same, with the floating windows the text holds. The tree is null when the text
    // has windows only; false means the text was refused.
    [[nodiscard]] static bool Parse(
        std::string_view text,
        const std::vector<std::uint32_t>& knownPanels,
        std::uint32_t& nextNodeId,
        std::unique_ptr<DockNode>& root,
        std::vector<DockNode>& floating);
};

} // namespace kb::editor
