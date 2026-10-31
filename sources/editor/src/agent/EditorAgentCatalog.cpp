#include "agent/EditorAgentCatalog.hpp"

#include "engine/core/JsonValue.hpp"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <ShlObj.h>
#endif

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <optional>
#include <sstream>
#include <system_error>

namespace kb::editor {
namespace {

[[nodiscard]] std::wstring Widen(std::string_view text) {
#if defined(_WIN32)
    if (text.empty()) {
        return {};
    }
    const int length = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
    std::wstring wide(static_cast<std::size_t>(std::max(length, 0)), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), wide.data(), length);
    return wide;
#else
    return std::wstring(text.begin(), text.end());
#endif
}

// The full path of a command on PATH, trying the executable and script extensions Windows runs.
[[nodiscard]] std::optional<std::filesystem::path> FindOnPath(std::wstring_view name) {
#if defined(_WIN32)
    const std::wstring command{ name };
    for (const wchar_t* extension : { L".exe", L".cmd", L".bat" }) {
        std::wstring found(MAX_PATH, L'\0');
        const DWORD length = SearchPathW(nullptr, command.c_str(), extension, static_cast<DWORD>(found.size()), found.data(), nullptr);
        if (length > 0U && length < found.size()) {
            found.resize(length);
            return std::filesystem::path{ found };
        }
    }
#else
    static_cast<void>(name);
#endif
    return std::nullopt;
}

[[nodiscard]] std::filesystem::path HomeFolder() {
#if defined(_WIN32)
    wchar_t profile[MAX_PATH]{};
    if (const DWORD length = GetEnvironmentVariableW(L"USERPROFILE", profile, MAX_PATH); length > 0U && length < MAX_PATH) {
        return std::filesystem::path{ profile };
    }
#else
    if (const char* home = std::getenv("HOME"); home != nullptr && home[0] != '\0') {
        return std::filesystem::path{ home };
    }
#endif
    return {};
}

// The CLI's default profile, then every "<folder>-<name>" folder beside it in the home folder:
// the way several accounts of one CLI are usually kept apart.
[[nodiscard]] std::vector<std::filesystem::path> ProfileFolders(std::wstring_view folder) {
    std::vector<std::filesystem::path> profiles{ std::filesystem::path{} };
    const std::filesystem::path home = HomeFolder();
    std::error_code error;
    if (home.empty() || !std::filesystem::is_directory(home, error)) {
        return profiles;
    }
    const std::wstring prefix = std::wstring{ folder } + L"-";
    std::vector<std::filesystem::path> found;
    for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator(home, error)) {
        const std::wstring name = entry.path().filename().wstring();
        if (name.size() > prefix.size() && name.compare(0, prefix.size(), prefix) == 0 && entry.is_directory(error)) {
            found.push_back(entry.path());
        }
    }
    std::ranges::sort(found);
    profiles.insert(profiles.end(), found.begin(), found.end());
    return profiles;
}

// One built-in agent: how to find its CLI and its profiles, and how to run it.
struct Preset {
    std::string name;
    std::wstring command;
    std::wstring profileFolder;
    std::wstring profileVariable;
    EditorAgent agent;
};

[[nodiscard]] std::vector<Preset> Presets() {
    std::vector<Preset> presets;
    {
        // Claude Code: one request on stdin, line-delimited JSON events, the engine's tools handed
        // over for this run only and allowed without asking, file edits accepted.
        EditorAgent agent;
        agent.runArguments = { "-p", "--output-format", "stream-json", "--verbose", "--mcp-config", "{mcpconfig}",
            "--allowedTools", "mcp__kb-engine", "--permission-mode", "acceptEdits" };
        agent.resumeArguments = { "--resume", "{session}" };
        agent.statusArguments = { "auth", "status", "--json" };
        agent.output = EditorAgentOutputFormat::ClaudeStreamJson;
        presets.push_back({ "Claude Code", L"claude", L".claude", L"CLAUDE_CONFIG_DIR", agent });
    }
    {
        // Codex: the engine's MCP server is passed as configuration overrides, so nothing is
        // written to the user's Codex configuration.
        EditorAgent agent;
        agent.runArguments = { "exec", "--json", "--skip-git-repo-check", "-C", "{project}", "-s", "workspace-write",
            "-c", "mcp_servers.kb-engine.command=\"{kbcli}\"",
            "-c", "mcp_servers.kb-engine.args=[\"mcp\",\"--project\",\"{project}\"]" };
        agent.statusArguments = { "login", "status" };
        agent.output = EditorAgentOutputFormat::CodexJson;
        presets.push_back({ "Codex", L"codex", L".codex", L"CODEX_HOME", agent });
    }
    {
        EditorAgent agent;
        agent.runArguments = { "--approval-mode", "auto_edit" };
        agent.connectArguments = { "mcp", "add", "kb-engine", "{kbcli}", "mcp", "--project", "{project}", "--trust" };
        presets.push_back({ "Gemini CLI", L"gemini", L"", L"", agent });
    }
    {
        EditorAgent agent;
        agent.runArguments = { "--approval-mode", "auto_edit" };
        agent.connectArguments = { "mcp", "add", "kb-engine", "{kbcli}", "mcp", "--project", "{project}", "--trust" };
        presets.push_back({ "Qwen Code", L"qwen", L"", L"", agent });
    }
    {
        EditorAgent agent;
        agent.runArguments = { "-p", "{prompt}", "--cwd", "{project}", "--always-approve" };
        presets.push_back({ "Grok", L"grok", L"", L"", agent });
    }
    return presets;
}

[[nodiscard]] std::vector<std::string> Strings(const kb::core::JsonValue* array) {
    std::vector<std::string> words;
    if (array == nullptr || array->GetKind() != kb::core::JsonValue::Kind::Array) {
        return words;
    }
    for (std::size_t index = 0; index < array->Size(); ++index) {
        if (const kb::core::JsonValue* word = array->At(index); word != nullptr && word->GetKind() == kb::core::JsonValue::Kind::String) {
            words.push_back(word->AsString());
        }
    }
    return words;
}

void AppendCustomAgents(std::vector<EditorAgent>& agents) {
    std::ifstream file(EditorAgentsFile(), std::ios::binary);
    if (!file) {
        return;
    }
    std::ostringstream text;
    text << file.rdbuf();
    kb::core::JsonValue document;
    std::string error;
    if (!kb::core::JsonValue::Parse(text.str(), document, error)) {
        return;
    }
    const kb::core::JsonValue* list = document.Find("agents");
    if (list == nullptr || list->GetKind() != kb::core::JsonValue::Kind::Array) {
        return;
    }
    for (std::size_t index = 0; index < list->Size(); ++index) {
        const kb::core::JsonValue* entry = list->At(index);
        const kb::core::JsonValue* name = entry != nullptr ? entry->Find("name") : nullptr;
        const kb::core::JsonValue* command = entry != nullptr ? entry->Find("command") : nullptr;
        if (name == nullptr || command == nullptr || name->AsString().empty() || command->AsString().empty()) {
            continue;
        }
        std::filesystem::path executable{ Widen(command->AsString()) };
        std::error_code exists;
        if (!executable.is_absolute() || !std::filesystem::exists(executable, exists)) {
            const std::optional<std::filesystem::path> found = FindOnPath(executable.wstring());
            if (!found.has_value()) {
                continue;
            }
            executable = *found;
        }
        EditorAgent agent;
        agent.name = name->AsString();
        agent.profile = "default";
        agent.executable = executable;
        agent.runArguments = Strings(entry->Find("arguments"));
        agent.connectArguments = Strings(entry->Find("connect"));
        agent.custom = true;
        if (const kb::core::JsonValue* output = entry->Find("output"); output != nullptr) {
            agent.output = output->AsString() == "claude" ? EditorAgentOutputFormat::ClaudeStreamJson
                : output->AsString() == "codex" ? EditorAgentOutputFormat::CodexJson
                : EditorAgentOutputFormat::PlainText;
        }
        if (const kb::core::JsonValue* environment = entry->Find("environment");
            environment != nullptr && environment->GetKind() == kb::core::JsonValue::Kind::Object) {
            for (std::size_t member = 0; member < environment->Size(); ++member) {
                agent.environment.emplace_back(Widen(environment->MemberName(member)), Widen(environment->At(member)->AsString()));
            }
        }
        agents.push_back(std::move(agent));
    }
}

} // namespace

std::string EditorAgent::Label() const {
    return profile == "default" ? name : name + " \xC2\xB7 " + profile;
}

std::vector<EditorAgent> DetectEditorAgents() {
    std::vector<EditorAgent> agents;
    for (const Preset& preset : Presets()) {
        const std::optional<std::filesystem::path> executable = FindOnPath(preset.command);
        if (!executable.has_value()) {
            continue;
        }
        const std::vector<std::filesystem::path> profiles = preset.profileFolder.empty()
            ? std::vector<std::filesystem::path>{ std::filesystem::path{} }
            : ProfileFolders(preset.profileFolder);
        for (const std::filesystem::path& folder : profiles) {
            EditorAgent agent = preset.agent;
            agent.name = preset.name;
            agent.executable = *executable;
            agent.profile = folder.empty() ? "default" : folder.filename().string();
            if (!folder.empty()) {
                agent.environment.emplace_back(preset.profileVariable, folder.wstring());
            }
            agents.push_back(std::move(agent));
        }
        // Local models: Codex runs against Ollama when it is installed, with no account at all.
        if (preset.command == L"codex" && FindOnPath(L"ollama").has_value()) {
            EditorAgent local = preset.agent;
            local.name = "Codex + Ollama (local)";
            local.profile = "default";
            local.executable = *executable;
            local.statusArguments.clear();
            local.runArguments.insert(local.runArguments.end(), { "--oss", "--local-provider", "ollama" });
            agents.push_back(std::move(local));
        }
    }
    AppendCustomAgents(agents);
    return agents;
}

std::filesystem::path EditorAgentsFile() {
#if defined(_WIN32)
    PWSTR local = nullptr;
    std::filesystem::path root;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_CREATE, nullptr, &local)) && local != nullptr) {
        root = std::filesystem::path{ local };
    }
    CoTaskMemFree(local);
    return root.empty() ? std::filesystem::path{} : root / "21kb" / "Agents.json";
#else
    return HomeFolder().empty() ? std::filesystem::path{} : HomeFolder() / ".config" / "21kb" / "Agents.json";
#endif
}

bool EnsureEditorAgentsFile() {
    const std::filesystem::path path = EditorAgentsFile();
    std::error_code error;
    if (path.empty() || std::filesystem::exists(path, error)) {
        return !path.empty();
    }
    std::filesystem::create_directories(path.parent_path(), error);
    std::ofstream file(path, std::ios::binary);
    if (!file) {
        return false;
    }
    // JSON has no comments, so the format is shown by an example the editor does not read.
    file << "{\n"
            "  \"agents\": [],\n"
            "  \"example\": {\n"
            "    \"name\": \"My agent\",\n"
            "    \"command\": \"my-agent\",\n"
            "    \"arguments\": [\"--prompt\", \"{prompt}\", \"--folder\", \"{project}\"],\n"
            "    \"environment\": { \"MY_AGENT_PROFILE\": \"work\" },\n"
            "    \"connect\": [\"mcp\", \"add\", \"kb-engine\", \"{kbcli}\", \"mcp\", \"--project\", \"{project}\"],\n"
            "    \"output\": \"plain\"\n"
            "  }\n"
            "}\n";
    return static_cast<bool>(file);
}

} // namespace kb::editor
