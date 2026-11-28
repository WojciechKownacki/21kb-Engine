#include "agent/EditorAgentCatalog.hpp"

#include "agent/EditorAgentGuidance.hpp"
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
#include <map>
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

// The newest file of `pattern` under `base`: a path whose last part may hold one * (the name the CLI gives its cache file is
// not fixed), or a plain path.
[[nodiscard]] std::filesystem::path ResolveModelsFile(const std::filesystem::path& base, const std::string& pattern) {
    const std::filesystem::path wanted = base / std::filesystem::path{ pattern };
    const std::string name = wanted.filename().string();
    const std::size_t star = name.find('*');
    if (star == std::string::npos) return wanted;
    const std::string head = name.substr(0U, star);
    const std::string tail = name.substr(star + 1U);
    std::filesystem::path newest;
    std::filesystem::file_time_type newestTime{};
    std::error_code error;
    for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator(wanted.parent_path(), error)) {
        const std::string candidate = entry.path().filename().string();
        if (candidate.size() < head.size() + tail.size() || !candidate.starts_with(head) || !candidate.ends_with(tail)) continue;
        const std::filesystem::file_time_type time = entry.last_write_time(error);
        if (newest.empty() || time > newestTime) {
            newest = entry.path();
            newestTime = time;
        }
    }
    return newest;
}

[[nodiscard]] std::vector<Preset> Presets() {
    std::vector<Preset> presets;
    {
        // Claude Code: one request on stdin, line-delimited JSON events, the engine's tools handed
        // over for this run only and allowed without asking, file edits accepted.
        EditorAgent agent;
        // Every step resends the tool definitions, so the agent gets only what it uses: the engine's
        // tools (--strict-mcp-config leaves out the MCP servers of the user's own setup) and the five
        // file tools for scripts (--tools); this halves the context of each step and lets the engine
        // tools load at once instead of through a search.
        agent.runArguments = { "-p", "--output-format", "stream-json", "--verbose", "--mcp-config", "{mcpconfig}", "--strict-mcp-config",
            "--tools", "Read", "Edit", "Write", "Glob", "Grep",
            // No skills or slash commands in the context, no per-session sections (date, git status)
            // that keep a new conversation from reusing the cached prompt, and no permission prompt
            // that would stall a run nobody can answer.
            "--disable-slash-commands", "--exclude-dynamic-system-prompt-sections", "--permission-prompts", "none",
            // It changes files only in the project (the working folder edits are accepted in); anywhere else it
            // may only read, so it never stops to ask, and a write outside the project is refused. It has no
            // shell, and kb_cli under the engine's tools refuses any path outside the project.
            "--allowedTools", "mcp__kb-engine", "Read", "Glob", "Grep", "--disallowedTools", "Bash", "PowerShell", "Agent", "Task",
            // The engine's own source folders are not for it to read; its docs and the SDK headers stay open.
            "{denyRead}",
            "--permission-mode", "{permissionMode}",
            "--append-system-prompt",
            "You work inside the 21kb editor on the open project; AGENTS.md says how, and which recipe fits the job. "
            "Use the kb-engine tools, not shell commands. A picture the user attaches is listed as a path: open it with Read. "
            "A picture you render with kb_cli capture is shown in the conversation." };
        agent.resumeArguments = { "--resume", "{session}" };
        agent.persistentArguments = { "--input-format", "stream-json" };
        agent.requestTemplate = "{\"type\":\"user\",\"message\":{\"role\":\"user\",\"content\":{request}}}\n";
        agent.statusArguments = { "auth", "status", "--json" };
        agent.signInArguments = { "auth", "login" };
        // The models it offers are those of its own catalog, as the CLI names them on its command line.
        agent.modelsFile = "cache/model-catalog/*-cc.json";
        agent.modelArguments = { "--model", "{model}" };
        agent.efforts = { "low", "medium", "high", "xhigh", "max" };
        agent.effortArguments = { "--effort", "{effort}" };
        agent.output = EditorAgentOutputFormat::StreamJson;
        agent.sessionRecords = "projects";
        presets.push_back({ "Claude Code", L"claude", L".claude", L"CLAUDE_CONFIG_DIR", agent });
    }
    {
        // Codex: the engine's MCP server is passed as configuration overrides, so nothing is
        // written to the user's Codex configuration. Its tools are approved up front: exec runs
        // with no one to ask, and a tool that waits for approval is refused there.
        EditorAgent agent;
        // This CLI calls its MCP tools from JavaScript in its exec tool, which no other CLI does. AGENTS.md is the same for all of
        // them, so how to call the tools here is said in this CLI's own instructions, and it does not spend a step looking them up.
        const std::string calling = "developer_instructions=\"The kb-engine tools are called from JavaScript in the exec tool, and the result is printed with text(result): "
            "tools.mcp__kb_engine__kb_cli({args:[...]}) or ({commands:[[...],[...]]}), tools.mcp__kb_engine__guide({topic:'recipe-...'}), "
            "tools.mcp__kb_engine__api_reference({topic:'...'}). Do not list or search the tool catalogue. Calls that do not depend on each other go in one exec, each awaited.\"";
        // Only the model the person chose works on the request: no sub-agents (features multi_agent and multi_agent_v2 off).
        agent.runArguments = { "exec", "--json", "--skip-git-repo-check", "-C", "{project}", "-s", "workspace-write",
            "--disable", "multi_agent", "--disable", "multi_agent_v2",
            "-c", "mcp_servers.kb-engine.command=\"{kbcli}\"",
            "-c", "mcp_servers.kb-engine.args=[\"mcp\",\"--project\",\"{project}\"]",
            "-c", "mcp_servers.kb-engine.default_tools_approval_mode=\"approve\"",
            "-c", calling };
        // A follow-up continues the thread (the process already runs in the project folder; resume
        // takes no -C or -s), with the request read from standard input.
        agent.resumeRunArguments = { "exec", "resume", "{session}", "--json", "--skip-git-repo-check",
            "--disable", "multi_agent", "--disable", "multi_agent_v2",
            "-c", "mcp_servers.kb-engine.command=\"{kbcli}\"",
            "-c", "mcp_servers.kb-engine.args=[\"mcp\",\"--project\",\"{project}\"]",
            "-c", "mcp_servers.kb-engine.default_tools_approval_mode=\"approve\"",
            "-c", calling, "-" };
        agent.statusArguments = { "login", "status" };
        agent.signInArguments = { "login" };
        // The models come from the CLI's own cache when the agent is detected; with none chosen the model stays the
        // one in the user's configuration. The choice is passed for a new run and when a thread resumes.
        agent.modelArguments = { "-m", "{model}" };
        agent.modelsFile = "models_cache.json";
        // What the CLI's own model list offers (its cache names low, medium, high, xhigh and max; "minimal" is not one of them).
        agent.efforts = { "low", "medium", "high", "xhigh", "max" };
        agent.effortArguments = { "-c", "model_reasoning_effort=\"{effort}\"" };
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
        agent.runArguments = { "-p", "{prompt}", "--cwd", "{project}", "--always-approve", "--no-subagents" };
        agent.efforts = { "low", "medium", "high" };
        agent.effortArguments = { "--reasoning-effort", "{effort}" };
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

// The agents file as a document; null when it is missing or damaged.
[[nodiscard]] kb::core::JsonValue AgentsDocument() {
    std::ifstream file(EditorAgentsFile(), std::ios::binary);
    kb::core::JsonValue document;
    if (!file) return document;
    std::ostringstream text;
    text << file.rdbuf();
    std::string error;
    if (!kb::core::JsonValue::Parse(text.str(), document, error)) return {};
    return document;
}

// The model order the user wrote for an agent, lightest first ("tiers": { "<agent name>": [ ... ] }).
[[nodiscard]] std::map<std::string, std::vector<std::string>> UserTiers() {
    std::map<std::string, std::vector<std::string>> tiers;
    const kb::core::JsonValue document = AgentsDocument();
    const kb::core::JsonValue* list = document.Find("tiers");
    if (list == nullptr || list->GetKind() != kb::core::JsonValue::Kind::Object) return tiers;
    for (std::size_t index = 0; index < list->Size(); ++index) {
        tiers[list->MemberName(index)] = Strings(list->At(index));
    }
    return tiers;
}

// The ids of the models a program lists among its main ones, in their order.
[[nodiscard]] std::vector<std::string> MainModelIds(const std::vector<EditorAgentModel>& models) {
    std::vector<std::string> ids;
    for (const EditorAgentModel& model : models) {
        if (model.main) ids.push_back(model.id);
    }
    return ids;
}

void AppendCustomAgents(std::vector<EditorAgent>& agents, const std::map<std::string, std::vector<std::string>>& tiers) {
    const kb::core::JsonValue document = AgentsDocument();
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
        // The models it can be started with, best first, and the words that pass the chosen one ({model}).
        for (const std::string& model : Strings(entry->Find("models"))) {
            agent.models.push_back(EditorAgentModel{ model, {}, true });
        }
        agent.modelArguments = Strings(entry->Find("modelArguments"));
        agent.ladder = ModelLadder(MainModelIds(agent.models), tiers.contains(agent.name) ? tiers.at(agent.name) : std::vector<std::string>{});
        if (const kb::core::JsonValue* output = entry->Find("output"); output != nullptr) {
            agent.output = output->AsString() == "stream-json" ? EditorAgentOutputFormat::StreamJson
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

std::vector<EditorAgentModel> ListedEditorAgentModels(const std::filesystem::path& file) {
    std::vector<EditorAgentModel> models;
    std::ifstream stream(file, std::ios::binary);
    if (!stream) {
        return models;
    }
    std::ostringstream text;
    text << stream.rdbuf();
    kb::core::JsonValue document;
    std::string error;
    if (!kb::core::JsonValue::Parse(text.str(), document, error)) {
        return models;
    }
    // The list is at the top of the file or under catalog.config.
    const kb::core::JsonValue* catalog = document.Find("catalog");
    const kb::core::JsonValue* config = catalog != nullptr ? catalog->Find("config") : nullptr;
    const kb::core::JsonValue* listed = config != nullptr ? config->Find("models") : document.Find("models");
    if (listed == nullptr || listed->GetKind() != kb::core::JsonValue::Kind::Array) {
        return models;
    }
    const auto textOf = [](const kb::core::JsonValue* value) {
        return value != nullptr && value->GetKind() == kb::core::JsonValue::Kind::String ? value->AsString() : std::string{};
    };
    for (std::size_t index = 0; index < listed->Size(); ++index) {
        const kb::core::JsonValue* model = listed->At(index);
        if (model == nullptr) continue;
        // The name the command line takes: the entry's slug, else its id.
        std::string id = textOf(model->Find("slug"));
        if (id.empty()) id = textOf(model->Find("id"));
        // The name the program shows for it: its name, else its display name.
        std::string name = textOf(model->Find("name"));
        if (name.empty()) name = textOf(model->Find("display_name"));
        const std::string section = textOf(model->Find("section"));
        const bool main = section.empty() || section == "main";
        const bool duplicate = std::ranges::any_of(models, [&id](const EditorAgentModel& listedModel) {
            return listedModel.id == id;
        });
        if (id.empty() || textOf(model->Find("visibility")) == "hide" || duplicate) {
            continue;
        }
        models.push_back(EditorAgentModel{ std::move(id), std::move(name), main });
    }
    return models;
}

EditorAgentPrices LoadEditorAgentPrices() {
    return EditorAgentPrices::FromJson(AgentsDocument());
}

std::string EditorAgent::Label() const {
    return profile == "default" ? name : name + " \xC2\xB7 " + profile;
}

std::string EditorAgentModel::Label() const {
    return name.empty() ? id : name + " (" + id + ")";
}

bool EditorAgent::OffersModel(const std::string& id) const {
    return std::ranges::any_of(models, [&id](const EditorAgentModel& model) {
        return model.id == id;
    });
}

std::string EditorAgent::ModelName(const std::string& id) const {
    const auto found = std::ranges::find_if(models, [&id](const EditorAgentModel& model) {
        return model.id == id;
    });
    return found == models.end() || found->name.empty() ? id : found->name;
}

std::vector<EditorAgent> DetectEditorAgents() {
    std::vector<EditorAgent> agents;
    const std::map<std::string, std::vector<std::string>> tiers = UserTiers();
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
            // The session records live in the profile's folder (the CLI's own one by default).
            if (!agent.sessionRecords.empty()) {
                agent.sessionRecords = (folder.empty() ? HomeFolder() / preset.profileFolder : folder) / agent.sessionRecords;
            }
            if (!agent.modelsFile.empty() && agent.models.empty()) {
                agent.models = ListedEditorAgentModels(ResolveModelsFile(folder.empty() ? HomeFolder() / preset.profileFolder : folder, agent.modelsFile));
            }
            agent.ladder = ModelLadder(MainModelIds(agent.models), tiers.contains(agent.name) ? tiers.at(agent.name) : std::vector<std::string>{});
            agents.push_back(std::move(agent));
        }
        // Local models: Codex runs against Ollama when it is installed, with no account at all.
        if (preset.command == L"codex" && FindOnPath(L"ollama").has_value()) {
            EditorAgent local = preset.agent;
            local.name = "Codex + Ollama (local)";
            local.profile = "default";
            local.executable = *executable;
            local.statusArguments.clear();
            local.models.clear();
            local.modelArguments.clear();
            local.efforts.clear();
            local.effortArguments.clear();
            local.runArguments.insert(local.runArguments.end(), { "--oss", "--local-provider", "ollama" });
            // resume has no --oss: a local conversation starts each request afresh.
            local.resumeRunArguments.clear();
            agents.push_back(std::move(local));
        }
    }
    AppendCustomAgents(agents, tiers);
    return agents;
}

std::filesystem::path EditorAgentsFile() {
    // Another file can be named, for a setup that keeps its preferences elsewhere or a run that must leave the user's own alone.
#if defined(_WIN32)
    wchar_t chosen[MAX_PATH]{};
    if (const DWORD length = GetEnvironmentVariableW(L"KB_EDITOR_AGENTS_FILE", chosen, MAX_PATH); length > 0U && length < MAX_PATH) {
        return std::filesystem::path{ chosen };
    }
#else
    if (const char* chosen = std::getenv("KB_EDITOR_AGENTS_FILE"); chosen != nullptr && chosen[0] != '\0') {
        return std::filesystem::path{ chosen };
    }
#endif
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
            // What the window shows as money, and the order it takes models in when it suggests one: both are the user's.
            // Prices are per million tokens; a tier list runs from the lightest model to the strongest.
            "  \"currency\": \"USD\",\n"
            "  \"prices\": {},\n"
            "  \"tiers\": {},\n"
            "  \"examplePrices\": { \"model-name\": { \"input\": 1.0, \"cacheRead\": 0.1, \"cacheWrite\": 1.25, \"output\": 5.0 } },\n"
            "  \"exampleTiers\": { \"My agent\": [\"lightest-model\", \"middle-model\", \"strongest-model\"] },\n"
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
