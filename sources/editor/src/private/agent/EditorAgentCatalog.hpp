#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

namespace kb::editor {

// How an agent's output is read: the line-delimited JSON events of Claude Code or of Codex, or
// plain text, one line after another.
enum class EditorAgentOutputFormat : std::uint8_t {
    ClaudeStreamJson,
    CodexJson,
    PlainText,
};

// One agent the Agent panel can run: a command-line agent found on this machine, with one of its
// sign-in profiles. The panel never signs in itself; it starts the CLI with the profile's folder
// and the CLI works with whatever account that folder is signed in to.
struct EditorAgent {
    std::string name;
    // "default" for the CLI's own folder, otherwise the folder's name (".claude-work").
    std::string profile;
    std::filesystem::path executable;
    // Variables added to the agent's environment, such as the profile folder.
    std::vector<std::pair<std::wstring, std::wstring>> environment;
    // The words after the executable for one request. The request text goes to the agent's
    // standard input unless a word holds {prompt}. Other placeholders: {project} (the project
    // folder), {kbcli} (kb_cli next to the editor) and {mcpconfig} (an MCP configuration file
    // naming the engine's tools).
    std::vector<std::string> runArguments;
    // Asks the CLI who is signed in; empty when it has no such command.
    std::vector<std::string> statusArguments;
    // Registers the engine's MCP server with the CLI once, for agents that cannot be handed it
    // on each run; empty when every run passes it.
    std::vector<std::string> connectArguments;
    // Continues the previous conversation with this session id ({session}); empty when each
    // request starts a new one.
    std::vector<std::string> resumeArguments;
    EditorAgentOutputFormat output = EditorAgentOutputFormat::PlainText;
    bool custom = false;

    // "Claude Code" for the default profile, "Claude Code - .claude-work" for another.
    [[nodiscard]] std::string Label() const;
};

// Every agent on this machine: each built-in preset whose CLI is on PATH, once per sign-in
// profile folder found in the user's home, followed by the user's own agents from
// EditorAgentsFile().
[[nodiscard]] std::vector<EditorAgent> DetectEditorAgents();

// The user's own agents (any CLI that takes a request and works in a folder), kept next to the
// other editor preferences. EnsureEditorAgentsFile writes an empty list with an example the
// first time, so there is a file to open from the panel.
[[nodiscard]] std::filesystem::path EditorAgentsFile();
bool EnsureEditorAgentsFile();

} // namespace kb::editor
