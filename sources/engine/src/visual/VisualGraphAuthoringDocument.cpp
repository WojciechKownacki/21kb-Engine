#include "engine/visual/VisualGraphAuthoringDocument.hpp"
#include "engine/visual/VisualGraphAssetWriter.hpp"
#include "engine/visual/VisualGraphCompiler.hpp"
#include "engine/visual/VisualGraphLiteral.hpp"
#include "visual/VisualGraphParser.hpp"
#include "scene/asset/io/SceneAssetBinaryIO.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <limits>
#include <locale>
#include <sstream>

namespace kb::visual {
namespace {
bool TextSafe(std::string_view text) { return text.find_first_of("\r\n#") == std::string_view::npos && text.find('\0') == std::string_view::npos; }
VisualGraphNode* Node(VisualGraphAuthoringSnapshot& state, std::uint32_t id) {
    for (auto& node : state.graph.nodes) if (node.id == id) return &node;
    return nullptr;
}
std::string Errors(const std::vector<std::string>& errors) {
    std::string output;
    for (const auto& entry : errors) { if (!output.empty()) output += '\n'; output += entry; }
    return output;
}
} // namespace

void VisualGraphAuthoringDocument::Create(std::string name) {
    VisualGraphDocument document;
    document.MutableGraph().name = TextSafe(name) && !name.empty() ? std::move(name) : "VisualGraph";
    const auto id = document.AddNode({.kind = VisualGraphNodeKind::Event, .lifecycle = VisualGraphLifecycleEvent::Tick}, VisualGraphNodeDefinitionRegistry::CreateDefault());
    history_.assign(1, {document.Graph(), {{id, 32, 32}}});
    cursor_ = 0; saved_.reset(); EndGroup();
}
const VisualGraphAuthoringSnapshot& VisualGraphAuthoringDocument::Snapshot() const noexcept {
    static const VisualGraphAuthoringSnapshot empty;
    return history_.empty() ? empty : history_[cursor_];
}
bool VisualGraphAuthoringDocument::Dirty() const noexcept { return !history_.empty() && (!saved_ || *saved_ != cursor_); }
bool VisualGraphAuthoringDocument::CanUndo() const noexcept { return cursor_ > 0; }
bool VisualGraphAuthoringDocument::CanRedo() const noexcept { return cursor_ + 1 < history_.size(); }
bool VisualGraphAuthoringDocument::Undo() noexcept { EndGroup(); if (!CanUndo()) return false; --cursor_; return true; }
bool VisualGraphAuthoringDocument::Redo() noexcept { EndGroup(); if (!CanRedo()) return false; ++cursor_; return true; }
void VisualGraphAuthoringDocument::BeginGroup() noexcept { grouped_ = true; groupEdited_ = false; }
void VisualGraphAuthoringDocument::EndGroup() noexcept { grouped_ = false; groupEdited_ = false; }
std::vector<VisualGraphDiagnostic> VisualGraphAuthoringDocument::Validate() const { return VisualGraphCompiler::Compile(Snapshot().graph).diagnostics; }

bool VisualGraphAuthoringDocument::Apply(const std::function<bool(VisualGraphAuthoringSnapshot&)>& edit, std::string& error) {
    error.clear();
    if (history_.empty()) { error = "Open a graph first"; return false; }
    auto candidate = Snapshot();
    if (!edit(candidate)) { if (error.empty()) error = "The graph edit is not applicable"; return false; }
    const auto compiled = VisualGraphCompiler::Compile(candidate.graph);
    if (!compiled.Succeeded()) { error = Errors(compiled.errors); return false; }
    if (grouped_ && groupEdited_) {
        if (saved_ && *saved_ == cursor_) saved_.reset();
        history_[cursor_] = std::move(candidate); return true;
    }
    if (saved_ && *saved_ > cursor_) saved_.reset();
    history_.resize(cursor_ + 1); history_.push_back(std::move(candidate)); ++cursor_;
    groupEdited_ = grouped_;
    if (history_.size() > 128) {
        history_.erase(history_.begin()); --cursor_;
        if (saved_) { if (*saved_ == 0) saved_.reset(); else --*saved_; }
    }
    return true;
}
std::uint32_t VisualGraphAuthoringDocument::AddNode(const VisualGraphNodeCatalogEntry& entry, float x, float y, std::string& error) {
    std::uint32_t id = 0;
    const bool changed = Apply([&](auto& state) {
        if (!std::isfinite(x) || !std::isfinite(y) || std::abs(x) > 1000000 || std::abs(y) > 1000000 || !TextSafe(entry.symbol)) return false;
        VisualGraphDocument document(state.graph); id = document.AddNode(entry);
        state.graph = document.Graph(); state.positions.push_back({id, x, y}); return true;
    }, error);
    return changed ? id : 0;
}
bool VisualGraphAuthoringDocument::RemoveNode(std::uint32_t id, std::string& error) {
    return Apply([&](auto& state) {
        VisualGraphDocument document(state.graph); if (!document.RemoveNode(id)) return false;
        state.graph = document.Graph(); std::erase_if(state.positions, [&](const auto& point) { return point.nodeId == id; }); return true;
    }, error);
}
bool VisualGraphAuthoringDocument::MoveNode(std::uint32_t id, float x, float y, std::string& error) {
    return Apply([&](auto& state) {
        if (!state.graph.FindNode(id) || !std::isfinite(x) || !std::isfinite(y) || std::abs(x) > 1000000 || std::abs(y) > 1000000) return false;
        for (auto& point : state.positions) if (point.nodeId == id) { if (point.x == x && point.y == y) return false; point.x = x; point.y = y; return true; }
        state.positions.push_back({id, x, y}); return true;
    }, error);
}
bool VisualGraphAuthoringDocument::SetLiteral(std::uint32_t id, std::string_view text, std::string& error) {
    return Apply([&](auto& state) {
        auto* node = Node(state, id); const auto* pin = state.graph.FindPin(id, "value", VisualGraphPinDirection::Output);
        if (!node || !pin || !VisualGraphLiteral::IsSymbol(node->symbol) || !VisualGraphLiteral::Parse(pin->type, text)) {
            error = "Literal text does not match the output type"; return false;
        }
        node->symbol = VisualGraphLiteral::Symbol(text); return true;
    }, error);
}
bool VisualGraphAuthoringDocument::SetEvent(std::uint32_t id, VisualGraphLifecycleEvent event, std::string& error) {
    return Apply([&](auto& state) { auto* node = Node(state, id); if (!node || node->kind != VisualGraphNodeKind::Event) return false; node->lifecycle = event; return true; }, error);
}
bool VisualGraphAuthoringDocument::SetSymbol(std::uint32_t id, std::string text, std::string& error) {
    return Apply([&](auto& state) {
        auto* node = Node(state, id);
        if (!node || !TextSafe(text) || (node->kind != VisualGraphNodeKind::Comment && node->kind != VisualGraphNodeKind::CustomEvent)) return false;
        node->symbol = std::move(text); return true;
    }, error);
}
bool VisualGraphAuthoringDocument::Connect(std::uint32_t from, std::string fromPin, std::uint32_t to, std::string toPin, std::string& error) {
    return Apply([&](auto& state) {
        const auto* output = state.graph.FindPin(from, fromPin, VisualGraphPinDirection::Output);
        if (!output) return false;
        for (const auto& edge : state.graph.edges)
            if (edge.kind == VisualGraphEdgeKind::Data && edge.toNode == to && edge.toPin == toPin) {
                error = "Disconnect the current input before replacing its connection"; return false;
            }
        VisualGraphDocument document(state.graph);
        const bool connected = output->type == VisualGraphValueType::Void ? document.ConnectExecution(from, fromPin, to, toPin) : document.ConnectData(from, fromPin, to, toPin);
        if (!connected) { error = "Pins are missing, occupied, or have incompatible types"; return false; }
        state.graph = document.Graph(); return true;
    }, error);
}
bool VisualGraphAuthoringDocument::Disconnect(std::size_t index, std::string& error) {
    return Apply([&](auto& state) { if (index >= state.graph.edges.size()) return false; state.graph.edges.erase(state.graph.edges.begin() + static_cast<std::ptrdiff_t>(index)); return true; }, error);
}
bool VisualGraphAuthoringDocument::Load(const std::filesystem::path& path, std::string& error) {
    error.clear(); std::ifstream input(path, std::ios::binary);
    if (!input) { error = "Graph file could not be opened"; return false; }
    const auto parsed = VisualGraphParser::Parse(input);
    if (!parsed.Succeeded()) { error = Errors(parsed.errors); return false; }
    VisualGraphAuthoringSnapshot state{parsed.graph, {}};
    input.clear(); input.seekg(0);
    for (std::string line; std::getline(input, line);) {
        if (!line.starts_with("# editor-position ")) continue;
        std::istringstream row(line.substr(18)); row.imbue(std::locale::classic());
        VisualGraphNodePosition point;
        if (!(row >> point.nodeId >> point.x >> point.y) || !state.graph.FindNode(point.nodeId) ||
            !std::isfinite(point.x) || !std::isfinite(point.y) || std::abs(point.x) > 1000000 || std::abs(point.y) > 1000000) continue;
        std::erase_if(state.positions, [&](const auto& prior) { return prior.nodeId == point.nodeId; }); state.positions.push_back(point);
    }
    for (std::size_t i = 0; i < state.graph.nodes.size(); ++i) {
        const auto id = state.graph.nodes[i].id;
        if (std::ranges::none_of(state.positions, [&](const auto& point) { return point.nodeId == id; }))
            state.positions.push_back({id, 32.0F + static_cast<float>(i % 4) * 240, 32.0F + static_cast<float>(i / 4) * 260});
    }
    history_.assign(1, std::move(state)); cursor_ = 0; saved_ = 0; EndGroup(); return true;
}
bool VisualGraphAuthoringDocument::Save(const std::filesystem::path& path, std::string& error) {
    error.clear(); const auto compiled = VisualGraphCompiler::Compile(Snapshot().graph);
    if (!compiled.Succeeded()) { error = Errors(compiled.errors); return false; }
    std::ostringstream output; output.imbue(std::locale::classic());
    VisualGraphAssetWriter::Write(output, Snapshot().graph);
    output << std::setprecision(std::numeric_limits<float>::max_digits10);
    for (const auto& point : Snapshot().positions) output << "# editor-position " << point.nodeId << ' ' << point.x << ' ' << point.y << '\n';
    const std::string text = output.str(); std::istringstream check(text);
    const auto parsed = VisualGraphParser::Parse(check);
    if (!parsed.Succeeded() || VisualGraphAssetWriter::WriteToString(parsed.graph) != VisualGraphAssetWriter::WriteToString(Snapshot().graph)) {
        error = "Graph text does not round-trip"; return false;
    }
    if (!output.good() || !kb::scene::SceneAssetBinaryIO::WriteBytesAtomically(path, {reinterpret_cast<const std::uint8_t*>(text.data()), text.size()})) {
        error = "Graph file could not be published atomically"; return false;
    }
    saved_ = cursor_; EndGroup(); return true;
}
} // namespace kb::visual
