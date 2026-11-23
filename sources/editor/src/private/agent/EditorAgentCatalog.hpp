#pragma once

#include "agent/EditorAgentPrices.hpp"

#include <cstdint>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

namespace kb::editor {

// How an agent's output is read: the line-delimited JSON events of Claude Code or of Codex, or
// plain text, one line after another.
enum class EditorAgentOutputFormat : std::uint8_t {
    StreamJson,
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
    // folder), {kbcli} (kb_cli next to the editor), {mcpconfig} (an MCP configuration file
    // naming the engine's tools) and {denyRead}, which becomes one "Read(//drive/folder/**)"
    // permission rule per engine folder the agent may not read (for a CLI that takes such rules).
    std::vector<std::string> runArguments;
    // Asks the CLI who is signed in; empty when it has no such command.
    std::vector<std::string> statusArguments;
    // Signs the profile in, in a console window of its own; empty when the CLI signs in by itself.
    std::vector<std::string> signInArguments;
    // Registers the engine's MCP server with the CLI once, for agents that cannot be handed it
    // on each run; empty when every run passes it.
    std::vector<std::string> connectArguments;
    // Continues the previous conversation with this session id ({session}); empty when each
    // request starts a new one.
    std::vector<std::string> resumeArguments;
    // For a CLI whose continuation is a command of its own (codex exec resume <id>): these replace
    // runArguments when a session continues, and resumeArguments are not added.
    std::vector<std::string> resumeRunArguments;
    // An agent that stays running between requests of one conversation: started once with these
    // arguments added, each request then goes to its standard input as `requestTemplate` with
    // {request} filled in (JSON-escaped), and a turn ends at its result event. Saves starting the
    // agent and its tools for every request.
    std::vector<std::string> persistentArguments;
    std::string requestTemplate;
    // Models to choose from, by the names the CLI takes; empty means only its own default.
    // {model} in modelArguments receives the choice; nothing is passed for the default.
    std::vector<std::string> models;
    std::vector<std::string> modelArguments;
    // A file in the profile folder where the CLI keeps the models it offers (the last part of the path may hold one *: the newest
    // such file is read); when `models` is empty the list is read from it as the agent is detected, so it is the CLI's own and
    // current. Empty `models` after that: the agent picks its model itself.
    std::string modelsFile;
    // `models` from the lightest to the strongest, to suggest one by the size of a request and to move one step up; the user's
    // own order for the agent when the agents file has one (Agents.json "tiers"), else the program's list turned round.
    std::vector<std::string> ladder;
    // Reasoning (effort) levels, lowest first, and how a chosen level is passed ({effort}).
    std::vector<std::string> efforts;
    std::vector<std::string> effortArguments;
    EditorAgentOutputFormat output = EditorAgentOutputFormat::PlainText;
    // Where the CLI keeps a record of every session, one folder per project named after the
    // project's path (every character but letters and digits a dash), one <session>.jsonl each;
    // empty when it keeps none the panel can read.
    std::filesystem::path sessionRecords;
    bool custom = false;

    // "Claude Code" for the default profile, "Claude Code - .claude-work" for another.
    [[nodiscard]] std::string Label() const;
};

// Every agent on this machine: each built-in preset whose CLI is on PATH, once per sign-in
// profile folder found in the user's home, followed by the user's own agents from
// EditorAgentsFile().
[[nodiscard]] std::vector<EditorAgent> DetectEditorAgents();

// The names of the models in a CLI's cache file, in the file's order, without the ones the CLI keeps out of its picker.
// Empty when the file is missing or unreadable.
[[nodiscard]] std::vector<std::string> ListedEditorAgentModels(const std::filesystem::path& file);
// The prices the user keeps in the agents file; no entry for any model when there are none.
[[nodiscard]] EditorAgentPrices LoadEditorAgentPrices();

// The user's own agents (any CLI that takes a request and works in a folder), kept next to the
// other editor preferences. EnsureEditorAgentsFile writes an empty list with an example the
// first time, so there is a file to open from the panel.
[[nodiscard]] std::filesystem::path EditorAgentsFile();
bool EnsureEditorAgentsFile();

} // namespace kb::editor
