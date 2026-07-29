#include "engine/scene/AiBehaviourAssetIO.hpp"

#include "scene/asset/io/SceneAssetBinaryIO.hpp"

#include <filesystem>
#include <iomanip>
#include <locale>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace kb::scene {
namespace {

constexpr std::string_view kMagic = "21kb-ai-behaviour";
constexpr std::uint32_t kVersion = 1U;

[[nodiscard]] const char* NodeKindName(AiNodeKind kind) noexcept {
    switch (kind) {
    case AiNodeKind::Sequence: return "Sequence";
    case AiNodeKind::Selector: return "Selector";
    case AiNodeKind::Condition: return "Condition";
    case AiNodeKind::Action: return "Action";
    case AiNodeKind::UtilitySelector: return "UtilitySelector";
    }
    return "";
}

[[nodiscard]] bool ParseNodeKind(std::string_view text, AiNodeKind& result) noexcept {
    if (text == "Sequence") result = AiNodeKind::Sequence;
    else if (text == "Selector") result = AiNodeKind::Selector;
    else if (text == "Condition") result = AiNodeKind::Condition;
    else if (text == "Action") result = AiNodeKind::Action;
    else if (text == "UtilitySelector") result = AiNodeKind::UtilitySelector;
    else return false;
    return true;
}

[[nodiscard]] bool WriteText(const std::filesystem::path& path, const std::string& text) {
    return SceneAssetBinaryIO::WriteBytesAtomically(
        path, std::span<const std::uint8_t>{ reinterpret_cast<const std::uint8_t*>(text.data()), text.size() });
}

} // namespace

std::optional<AiBehaviourAsset> AiBehaviourAssetIO::Load(const std::filesystem::path& path) {
    if (path.extension() != kAiBehaviourAssetExtension) return std::nullopt;
    const std::vector<std::uint8_t> bytes = SceneAssetBinaryIO::ReadAllBytes(path);
    if (bytes.empty()) return std::nullopt;
    std::istringstream input{ std::string{ reinterpret_cast<const char*>(bytes.data()), bytes.size() } };
    std::string magic;
    std::uint32_t version = 0U;
    AiBehaviourAsset asset;
    std::size_t nodeCount = 0U;
    std::size_t stateCount = 0U;
    if (!(input >> magic >> version >> asset.rootNode >> nodeCount >> asset.initialState >> stateCount) || magic != kMagic || version != kVersion ||
        nodeCount == 0U || nodeCount > 4096U || stateCount > 256U) return std::nullopt;
    asset.nodes.resize(nodeCount);
    for (AiBehaviourNode& node : asset.nodes) {
        std::string kind;
        if (!(input >> node.id >> kind >> node.firstChild >> node.childCount) || !ParseNodeKind(kind, node.kind)) return std::nullopt;
    }
    asset.states.resize(stateCount);
    for (AiState& state : asset.states) {
        std::size_t transitionCount = 0U;
        if (!(input >> std::quoted(state.name) >> state.rootNode >> transitionCount) || transitionCount > 1024U) return std::nullopt;
        state.transitions.resize(transitionCount);
        for (AiStateTransition& transition : state.transitions) {
            if (!(input >> transition.targetState >> transition.condition)) return std::nullopt;
        }
    }
    input >> std::ws;
    if (!input.eof() || !ValidateAiBehaviourAsset(asset).valid) return std::nullopt;
    return asset;
}

bool AiBehaviourAssetIO::Save(const std::filesystem::path& path, const AiBehaviourAsset& asset) {
    if (path.extension() != kAiBehaviourAssetExtension || !ValidateAiBehaviourAsset(asset).valid) return false;
    std::ostringstream output;
    output.imbue(std::locale::classic());
    output << kMagic << ' ' << kVersion << ' ' << asset.rootNode << ' ' << asset.nodes.size() << ' ' << asset.initialState << ' ' << asset.states.size() << '\n';
    for (const AiBehaviourNode& node : asset.nodes) {
        output << node.id << ' ' << NodeKindName(node.kind) << ' ' << node.firstChild << ' ' << node.childCount << '\n';
    }
    for (const AiState& state : asset.states) {
        output << std::quoted(state.name) << ' ' << state.rootNode << ' ' << state.transitions.size() << '\n';
        for (const AiStateTransition& transition : state.transitions) {
            output << transition.targetState << ' ' << transition.condition << '\n';
        }
    }
    return static_cast<bool>(output) && WriteText(path, output.str());
}

} // namespace kb::scene
