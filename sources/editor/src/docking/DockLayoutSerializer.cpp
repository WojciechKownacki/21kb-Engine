#include "docking/DockLayoutSerializer.hpp"

#include <algorithm>
#include <iomanip>
#include <locale>
#include <sstream>
#include <unordered_set>

namespace kb::editor {
namespace {

void SerializeLeaf(const DockNode& node, char tag, std::ostringstream& output) {
    output << ' ' << tag << ' ' << node.activePanelId << ' ' << node.panels.size();
    for (const std::uint32_t panelId : node.panels) {
        output << ' ' << panelId;
    }
}

void SerializeNode(const DockNode& node, std::ostringstream& output) {
    if (node.kind == DockNode::Kind::Split) {
        output << " S " << (node.axis == DockSplitAxis::Horizontal ? 'H' : 'V') << ' ' << node.ratio;
        if (node.first != nullptr) {
            SerializeNode(*node.first, output);
        }
        if (node.second != nullptr) {
            SerializeNode(*node.second, output);
        }
        return;
    }

    SerializeLeaf(node, 'L', output);
}

class TokenReader {
public:
    explicit TokenReader(std::string_view text)
        : stream_(std::string{text}) {
        stream_.imbue(std::locale::classic());
    }

    [[nodiscard]] bool Read(std::string& token) {
        return static_cast<bool>(stream_ >> token);
    }

    [[nodiscard]] bool Read(std::uint32_t& value) {
        return static_cast<bool>(stream_ >> value);
    }

    [[nodiscard]] bool Read(float& value) {
        return static_cast<bool>(stream_ >> value);
    }

    [[nodiscard]] bool NextIsFloating() {
        stream_ >> std::ws;
        return stream_.peek() == 'F';
    }

private:
    std::istringstream stream_;
};

struct ParseState {
    TokenReader& reader;
    const std::unordered_set<std::uint32_t>& knownPanels;
    std::unordered_set<std::uint32_t>& claimed;
    std::uint32_t& nextNodeId;
    std::uint32_t depth = 0U;
};

// A saved file is untrusted input: bound the recursion rather than trusting the
// stream to terminate.
constexpr std::uint32_t kMaximumDepth = 32U;

// The tabs of a leaf, after its tag: the active panel, the count and the panels.
[[nodiscard]] bool ParseLeafBody(ParseState& state, DockNode& node) {
    std::uint32_t activePanelId = 0U;
    std::uint32_t count = 0U;
    if (!state.reader.Read(activePanelId) || !state.reader.Read(count) || count > 64U) {
        return false;
    }
    node.kind = DockNode::Kind::Leaf;
    for (std::uint32_t index = 0U; index < count; ++index) {
        std::uint32_t panelId = 0U;
        if (!state.reader.Read(panelId)) {
            return false;
        }
        // A layout naming a panel this build does not have, or naming one twice, is
        // refused outright - half a workspace is worse than the default one.
        if (!state.knownPanels.contains(panelId) || !state.claimed.insert(panelId).second) {
            return false;
        }
        node.panels.push_back(panelId);
    }
    if (node.panels.empty()) {
        return false;
    }
    node.activePanelId = std::ranges::find(node.panels, activePanelId) == node.panels.end()
        ? node.panels.front()
        : activePanelId;
    return true;
}

[[nodiscard]] std::unique_ptr<DockNode> ParseNode(ParseState& state) {
    if (state.depth >= kMaximumDepth) {
        return nullptr;
    }

    std::string kind;
    if (!state.reader.Read(kind)) {
        return nullptr;
    }

    auto node = std::make_unique<DockNode>();
    node->id = state.nextNodeId++;

    if (kind == "S") {
        std::string axis;
        float ratio = 0.0F;
        if (!state.reader.Read(axis) || !state.reader.Read(ratio) ||
            (axis != "H" && axis != "V") || !(ratio > 0.0F) || !(ratio < 1.0F)) {
            return nullptr;
        }
        node->kind = DockNode::Kind::Split;
        node->axis = axis == "H" ? DockSplitAxis::Horizontal : DockSplitAxis::Vertical;
        node->ratio = ratio;
        ++state.depth;
        node->first = ParseNode(state);
        node->second = ParseNode(state);
        --state.depth;
        return node->first != nullptr && node->second != nullptr ? std::move(node) : nullptr;
    }

    if (kind != "L" || !ParseLeafBody(state, *node)) {
        return nullptr;
    }
    return node;
}

} // namespace

std::string DockLayoutSerializer::Serialize(const DockNode* root) {
    return Serialize(root, {});
}

std::string DockLayoutSerializer::Serialize(const DockNode* root, const std::vector<DockNode>& floating) {
    std::ostringstream output;
    output.imbue(std::locale::classic());
    // Enough digits that a splitter a person dragged comes back on the same pixel.
    output << std::setprecision(9);
    if (root != nullptr) {
        SerializeNode(*root, output);
    }
    for (const DockNode& group : floating) {
        if (group.panels.size() > 1U) {
            SerializeLeaf(group, 'F', output);
        }
    }
    std::string text = output.str();
    if (!text.empty() && text.front() == ' ') {
        text.erase(text.begin());
    }
    return text;
}

std::unique_ptr<DockNode> DockLayoutSerializer::Parse(
    std::string_view text,
    const std::vector<std::uint32_t>& knownPanels,
    std::uint32_t& nextNodeId) {
    std::unique_ptr<DockNode> root;
    std::vector<DockNode> floating;
    return Parse(text, knownPanels, nextNodeId, root, floating) ? std::move(root) : nullptr;
}

bool DockLayoutSerializer::Parse(
    std::string_view text,
    const std::vector<std::uint32_t>& knownPanels,
    std::uint32_t& nextNodeId,
    std::unique_ptr<DockNode>& root,
    std::vector<DockNode>& floating) {
    if (text.empty()) {
        return false;
    }

    const std::unordered_set<std::uint32_t> known{knownPanels.begin(), knownPanels.end()};
    std::unordered_set<std::uint32_t> claimed;
    TokenReader reader{text};
    std::uint32_t candidateNodeId = 1U;
    ParseState state{
        .reader = reader,
        .knownPanels = known,
        .claimed = claimed,
        .nextNodeId = candidateNodeId,
    };

    std::unique_ptr<DockNode> parsedRoot;
    if (!reader.NextIsFloating()) {
        parsedRoot = ParseNode(state);
        if (parsedRoot == nullptr) {
            return false;
        }
    }
    std::vector<DockNode> groups;
    std::string tag;
    while (reader.Read(tag)) {
        DockNode group;
        group.id = candidateNodeId++;
        if (tag != "F" || !ParseLeafBody(state, group)) {
            return false;
        }
        groups.push_back(std::move(group));
    }
    nextNodeId = candidateNodeId;
    root = std::move(parsedRoot);
    floating = std::move(groups);
    return true;
}

} // namespace kb::editor
