#include "engine/project/AgentRequestContext.hpp"

#include <algorithm>
#include <fstream>
#include <iterator>

namespace kb::project {
namespace {

constexpr std::size_t kNamedObjects = 8U;

} // namespace

std::string AgentProjectNotes(const std::filesystem::path& project) {
    std::ifstream input{ project / std::string{ kAgentNotesFile }, std::ios::binary };
    if (!input) return {};
    std::string text{ std::istreambuf_iterator<char>{ input }, std::istreambuf_iterator<char>{} };
    while (!text.empty() && (text.back() == '\n' || text.back() == '\r' || text.back() == ' ')) text.pop_back();
    if (text.empty()) return {};
    const std::size_t size = text.size();
    std::string cut;
    if (size > kAgentNotesLimit) {
        std::size_t keep = kAgentNotesLimit;
        // Not in the middle of a UTF-8 character.
        while (keep > 0U && (static_cast<unsigned char>(text[keep]) & 0xC0U) == 0x80U) --keep;
        text.resize(keep);
        cut = "\n[" + std::string{ kAgentNotesFile } + " was cut: " + std::to_string(keep) + " of " + std::to_string(size) + " bytes shown]";
    }
    return "Project notes (" + std::string{ kAgentNotesFile } + "):\n" + text + cut + "\n\n";
}

std::string AgentEditorContextLine(std::string_view scene, std::span<const std::string> selected) {
    std::string line = "Editor: ";
    line += scene.empty() ? "no scene open" : "scene " + std::string{ scene } + " open";
    if (selected.empty()) return line + ", nothing selected.\n\n";
    line += ", selected: ";
    const std::size_t shown = std::min(selected.size(), kNamedObjects);
    for (std::size_t index = 0U; index < shown; ++index) line += (index == 0U ? "" : ", ") + selected[index];
    if (selected.size() > shown) line += " and " + std::to_string(selected.size() - shown) + " more";
    return line + ".\n\n";
}

} // namespace kb::project
