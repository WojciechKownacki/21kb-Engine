#pragma once

#include <filesystem>
#include <span>
#include <string>
#include <string_view>

namespace kb::project {

// The project's own notes for the agent: AgentNotes.md in the project folder, sent with every request. A file over the limit is
// cut, and the text says so with the sizes. Empty when the project has no notes.
inline constexpr std::string_view kAgentNotesFile = "AgentNotes.md";
inline constexpr std::size_t kAgentNotesLimit = 8192U;
[[nodiscard]] std::string AgentProjectNotes(const std::filesystem::path& project);

// One line about what the person has open in the editor: the scene and the selected objects (the first few by name).
[[nodiscard]] std::string AgentEditorContextLine(std::string_view scene, std::span<const std::string> selected);

} // namespace kb::project
