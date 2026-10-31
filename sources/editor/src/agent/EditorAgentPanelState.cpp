#include "agent/EditorAgentPanelState.hpp"

#include "engine/core/JsonValue.hpp"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif

#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>
#include <system_error>

namespace kb::editor {
namespace {

using kb::core::JsonValue;

[[nodiscard]] std::wstring Widen(std::string_view text) {
#if defined(_WIN32)
    if (text.empty()) return {};
    const int length = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
    std::wstring wide(static_cast<std::size_t>(std::max(length, 0)), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), wide.data(), length);
    return wide;
#else
    return std::wstring(text.begin(), text.end());
#endif
}

void AppendUtf8(std::string& text, char32_t code) {
    if (code < 0x80U) {
        text.push_back(static_cast<char>(code));
    } else if (code < 0x800U) {
        text.push_back(static_cast<char>(0xC0U | (code >> 6U)));
        text.push_back(static_cast<char>(0x80U | (code & 0x3FU)));
    } else if (code < 0x10000U) {
        text.push_back(static_cast<char>(0xE0U | (code >> 12U)));
        text.push_back(static_cast<char>(0x80U | ((code >> 6U) & 0x3FU)));
        text.push_back(static_cast<char>(0x80U | (code & 0x3FU)));
    } else {
        text.push_back(static_cast<char>(0xF0U | (code >> 18U)));
        text.push_back(static_cast<char>(0x80U | ((code >> 12U) & 0x3FU)));
        text.push_back(static_cast<char>(0x80U | ((code >> 6U) & 0x3FU)));
        text.push_back(static_cast<char>(0x80U | (code & 0x3FU)));
    }
}

[[nodiscard]] std::filesystem::path SelectionFile() {
    const std::filesystem::path agents = EditorAgentsFile();
    return agents.empty() ? std::filesystem::path{} : agents.parent_path() / "AgentSelection.txt";
}

[[nodiscard]] std::string Replace(std::string text, std::string_view from, std::string_view to) {
    for (std::size_t at = text.find(from); at != std::string::npos; at = text.find(from, at + to.size())) {
        text.replace(at, from.size(), to);
    }
    return text;
}

[[nodiscard]] const std::string& StringOf(const JsonValue* value) {
    static const std::string empty;
    return value != nullptr && value->GetKind() == JsonValue::Kind::String ? value->AsString() : empty;
}

// A tool call as one short line: the tool without the engine server's prefix and the first
// argument that says what it worked on.
[[nodiscard]] std::string ToolLine(std::string name, const JsonValue* input) {
    constexpr std::string_view enginePrefix = "mcp__kb-engine__";
    if (name.starts_with(enginePrefix)) {
        name.erase(0, enginePrefix.size());
    }
    if (input != nullptr && input->GetKind() == JsonValue::Kind::Object) {
        for (const char* key : { "entity", "name", "scene", "path", "file_path", "source", "command", "type" }) {
            if (const std::string& value = StringOf(input->Find(key)); !value.empty()) {
                return name + "  " + (value.size() > 80U ? value.substr(0, 80U) + "..." : value);
            }
        }
    }
    return name;
}

// Agents answer in Markdown; the panel shows plain text, so the markers go and lists keep a bullet.
[[nodiscard]] std::string WithoutMarkdown(std::string text) {
    text = Replace(Replace(std::move(text), "**", ""), "__", "");
    std::erase(text, '`');
    std::string out;
    std::size_t start = 0;
    while (start <= text.size()) {
        const std::size_t end = std::min(text.find('\n', start), text.size());
        std::string line = text.substr(start, end - start);
        std::size_t hashes = 0;
        while (hashes < line.size() && line[hashes] == '#') ++hashes;
        if (hashes > 0U && hashes < line.size() && line[hashes] == ' ') {
            line.erase(0, hashes + 1);
        } else if (line.starts_with("- ") || line.starts_with("* ")) {
            line = "\xE2\x80\xA2 " + line.substr(2);
        }
        out += line;
        if (end < text.size()) out += '\n';
        start = end + 1;
    }
    return out;
}

// How many files the project holds and when the newest of them was written: enough to tell
// whether a request changed anything, without comparing contents.
void ProjectStamp(const std::filesystem::path& project, std::uintmax_t& files, std::filesystem::file_time_type& newest) {
    files = 0U;
    newest = {};
    std::error_code error;
    const std::filesystem::path assets = project / "Assets";
    for (std::filesystem::recursive_directory_iterator it(assets, std::filesystem::directory_options::skip_permission_denied, error), end;
         !error && it != end; it.increment(error)) {
        if (it->is_regular_file(error)) {
            ++files;
            newest = std::max(newest, it->last_write_time(error));
        }
    }
}

[[nodiscard]] std::string Shorten(std::string text, std::size_t limit = 300U) {
    if (text.size() > limit) {
        text.resize(limit);
        text += "...";
    }
    return text;
}

} // namespace

EditorAgentPanelState::EditorAgentPanelState() = default;
EditorAgentPanelState::~EditorAgentPanelState() = default;

const EditorAgent* EditorAgentPanelState::Current() const noexcept {
    return current_ >= 0 && current_ < static_cast<int>(agents_.size()) ? &agents_[static_cast<std::size_t>(current_)] : nullptr;
}

void EditorAgentPanelState::RefreshAgents() {
    const std::string previous = Current() != nullptr ? Current()->Label() : std::string{};
    std::string remembered = previous;
    if (remembered.empty()) {
        std::ifstream file(SelectionFile(), std::ios::binary);
        std::getline(file, remembered);
    }
    prepared_ = true;
    agents_ = DetectEditorAgents();
    current_ = agents_.empty() ? -1 : 0;
    for (int index = 0; index < static_cast<int>(agents_.size()); ++index) {
        if (agents_[static_cast<std::size_t>(index)].Label() == remembered) {
            current_ = index;
            break;
        }
    }
    StartAccountCheck();
}

void EditorAgentPanelState::Prepare() {
    if (!prepared_) {
        prepared_ = true;
        RefreshAgents();
    }
}

void EditorAgentPanelState::SelectAgent(int index) {
    if (index < 0 || index >= static_cast<int>(agents_.size()) || index == current_) {
        return;
    }
    current_ = index;
    RememberSelection();
    StartAccountCheck();
}

void EditorAgentPanelState::RememberSelection() const {
    const std::filesystem::path path = SelectionFile();
    if (path.empty() || Current() == nullptr) {
        return;
    }
    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    file << Current()->Label() << '\n';
}

void EditorAgentPanelState::StartAccountCheck() {
    account_.clear();
    signedIn_ = false;
    accountOutput_.clear();
    checkingAccount_ = false;
    const EditorAgent* agent = Current();
    if (agent == nullptr) {
        return;
    }
    if (agent->statusArguments.empty()) {
        // Nothing to ask: the agent is installed, and it will say so itself if it needs a sign-in.
        signedIn_ = true;
        return;
    }
    std::vector<std::wstring> arguments;
    for (const std::string& word : agent->statusArguments) {
        arguments.push_back(Widen(word));
    }
    accountCheck_ = std::make_unique<EditorAgentProcess>();
    std::string error;
    checkingAccount_ = accountCheck_->Start(agent->executable, arguments, {}, agent->environment, {}, error);
}

void EditorAgentPanelState::FinishAccountCheck() {
    checkingAccount_ = false;
    const EditorAgent* agent = Current();
    if (agent == nullptr) {
        return;
    }
    std::string text;
    for (const std::string& line : accountOutput_) {
        text += line;
        text += '\n';
    }
    if (agent->output == EditorAgentOutputFormat::ClaudeStreamJson) {
        JsonValue status;
        std::string error;
        if (JsonValue::Parse(text, status, error)) {
            const JsonValue* loggedIn = status.Find("loggedIn");
            signedIn_ = loggedIn != nullptr && loggedIn->AsBool();
            std::string plan = StringOf(status.Find("subscriptionType"));
            if (!plan.empty()) {
                plan[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(plan[0])));
            }
            account_ = StringOf(status.Find("email"));
            if (!plan.empty()) {
                account_ += account_.empty() ? plan : " \xC2\xB7 " + plan;
            }
        }
        return;
    }
    // Codex prints one sentence: "Logged in using ChatGPT" or "Not logged in".
    signedIn_ = text.find("Logged in") != std::string::npos && text.find("Not logged in") == std::string::npos;
    if (const std::size_t using_ = text.find("using "); signedIn_ && using_ != std::string::npos) {
        account_ = text.substr(using_ + 6U);
        while (!account_.empty() && (account_.back() == '\n' || account_.back() == '\r')) account_.pop_back();
    }
}

void EditorAgentPanelState::NewConversation() {
    entries_.clear();
    session_.clear();
    sessionAgent_.clear();
    scrollFromBottom_ = 0;
}

void EditorAgentPanelState::FocusInput(bool focused) noexcept {
    inputFocused_ = focused;
    pendingHighSurrogate_ = 0;
}

void EditorAgentPanelState::TypeCharacter(wchar_t character) {
    if (character >= 0xD800 && character <= 0xDBFF) {
        pendingHighSurrogate_ = character;
        return;
    }
    if (character >= 0xDC00 && character <= 0xDFFF) {
        if (pendingHighSurrogate_ != 0) {
            const char32_t code = 0x10000U + ((static_cast<char32_t>(pendingHighSurrogate_) - 0xD800U) << 10U) +
                (static_cast<char32_t>(character) - 0xDC00U);
            AppendUtf8(input_, code);
        }
        pendingHighSurrogate_ = 0;
        return;
    }
    pendingHighSurrogate_ = 0;
    if (character >= 0x20) {
        AppendUtf8(input_, static_cast<char32_t>(character));
    }
}

void EditorAgentPanelState::InsertText(std::string_view text) {
    for (const char character : text) {
        if (character != '\r') {
            input_.push_back(character == '\t' ? ' ' : character);
        }
    }
}

void EditorAgentPanelState::Backspace() {
    // One whole character, however many UTF-8 bytes it took.
    while (!input_.empty() && (static_cast<unsigned char>(input_.back()) & 0xC0U) == 0x80U) {
        input_.pop_back();
    }
    if (!input_.empty()) {
        input_.pop_back();
    }
}

bool EditorAgentPanelState::Running() const noexcept {
    return running_;
}

bool EditorAgentPanelState::Send(const EditorAgentWorkspace& workspace) {
    const EditorAgent* agent = Current();
    std::string request = input_;
    while (!request.empty() && (request.back() == ' ' || request.back() == '\n')) request.pop_back();
    if (running_ || request.empty()) {
        return false;
    }
    if (agent == nullptr) {
        Add(EditorAgentEntryKind::Error, "No agent is installed. Install Claude Code, Codex, Gemini CLI, Qwen Code or Grok, "
            "or add your own in the agents file, then choose Refresh in the agent menu.");
        return false;
    }

    // The engine's tools for agents that take an MCP configuration file on each run.
    const std::filesystem::path mcpConfig = workspace.project / "Saved" / "Agent" / "mcp.json";
    {
        std::error_code error;
        std::filesystem::create_directories(mcpConfig.parent_path(), error);
        JsonValue arguments = JsonValue::MakeArray();
        arguments.Append(JsonValue::MakeString("mcp"));
        arguments.Append(JsonValue::MakeString("--project"));
        arguments.Append(JsonValue::MakeString(workspace.project.generic_string()));
        JsonValue server = JsonValue::MakeObject();
        server.Set("command", JsonValue::MakeString(workspace.kbcli.generic_string()));
        server.Set("args", std::move(arguments));
        JsonValue servers = JsonValue::MakeObject();
        servers.Set("kb-engine", std::move(server));
        JsonValue document = JsonValue::MakeObject();
        document.Set("mcpServers", std::move(servers));
        std::ofstream file(mcpConfig, std::ios::binary | std::ios::trunc);
        file << document.Dump();
    }

    bool promptInArguments = false;
    const auto substitute = [&](const std::string& word) {
        std::string text = Replace(word, "{project}", workspace.project.generic_string());
        text = Replace(text, "{kbcli}", workspace.kbcli.generic_string());
        text = Replace(text, "{mcpconfig}", mcpConfig.generic_string());
        text = Replace(text, "{session}", session_);
        if (text.find("{prompt}") != std::string::npos) {
            promptInArguments = true;
            text = Replace(text, "{prompt}", request);
        }
        return Widen(text);
    };
    std::vector<std::wstring> arguments;
    for (const std::string& word : agent->runArguments) {
        arguments.push_back(substitute(word));
    }
    if (!session_.empty() && sessionAgent_ == agent->Label()) {
        for (const std::string& word : agent->resumeArguments) {
            arguments.push_back(substitute(word));
        }
    }

    // kb_cli first on PATH, so an agent that runs commands itself finds the engine's tool too.
    std::vector<std::pair<std::wstring, std::wstring>> environment = agent->environment;
#if defined(_WIN32)
    std::wstring path(32767, L'\0');
    path.resize(GetEnvironmentVariableW(L"PATH", path.data(), static_cast<DWORD>(path.size())));
    environment.emplace_back(L"PATH", workspace.kbcli.parent_path().wstring() + L";" + path);
#endif

    Add(EditorAgentEntryKind::User, request);
    input_.clear();
    project_ = workspace.project;
    ProjectStamp(project_, projectFiles_, projectNewest_);
    changedProject_ = false;
    scrollFromBottom_ = 0;
    sawError_ = false;
    run_ = std::make_unique<EditorAgentProcess>();
    std::string error;
    if (!run_->Start(agent->executable, arguments, workspace.project, environment, promptInArguments ? std::string{} : request, error)) {
        Add(EditorAgentEntryKind::Error, agent->Label() + ": " + error + ".");
        return false;
    }
    running_ = true;
    requestRun_ = true;
    started_ = std::chrono::steady_clock::now();
    return true;
}

bool EditorAgentPanelState::ConnectEngineTools(const EditorAgentWorkspace& workspace) {
    const EditorAgent* agent = Current();
    if (agent == nullptr || agent->connectArguments.empty() || running_) {
        return false;
    }
    std::vector<std::wstring> arguments;
    for (const std::string& word : agent->connectArguments) {
        std::string text = Replace(word, "{project}", workspace.project.generic_string());
        arguments.push_back(Widen(Replace(text, "{kbcli}", workspace.kbcli.generic_string())));
    }
    Add(EditorAgentEntryKind::Info, "Connecting the engine's tools to " + agent->Label() + "...");
    run_ = std::make_unique<EditorAgentProcess>();
    std::string error;
    if (!run_->Start(agent->executable, arguments, workspace.project, agent->environment, {}, error)) {
        Add(EditorAgentEntryKind::Error, error + ".");
        return false;
    }
    running_ = true;
    requestRun_ = false;
    sawError_ = false;
    started_ = std::chrono::steady_clock::now();
    return true;
}

void EditorAgentPanelState::Stop() {
    if (run_ != nullptr && running_) {
        run_->Stop();
        Add(EditorAgentEntryKind::Info, "Stopped.");
        sawError_ = true;
    }
}

void EditorAgentPanelState::Add(EditorAgentEntryKind kind, std::string text) {
    entries_.push_back(EditorAgentEntry{ kind, std::move(text) });
    // A new line keeps the view where the reader left it, unless they were at the newest line.
}

bool EditorAgentPanelState::Pump(bool& finished) {
    finished = false;
    bool changed = false;
    if (accountCheck_ != nullptr && checkingAccount_) {
        for (std::string& line : accountCheck_->TakeLines()) {
            accountOutput_.push_back(std::move(line));
        }
        if (accountCheck_->ExitCode().has_value()) {
            FinishAccountCheck();
            changed = true;
        }
    }
    if (run_ == nullptr || !running_) {
        return changed;
    }
    for (const std::string& line : run_->TakeLines()) {
        ReadLine(line);
        changed = true;
    }
    if (const std::optional<int> code = run_->ExitCode(); code.has_value()) {
        running_ = false;
        finished = requestRun_;
        changed = true;
        if (requestRun_) {
            std::uintmax_t files = 0U;
            std::filesystem::file_time_type newest{};
            ProjectStamp(project_, files, newest);
            changedProject_ = files != projectFiles_ || newest != projectNewest_;
        }
        if (*code != 0 && !sawError_) {
            Add(EditorAgentEntryKind::Error, "The agent stopped with exit code " + std::to_string(*code) + ".");
        }
        const auto seconds = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - started_).count();
        Add(EditorAgentEntryKind::Info, "Finished in " + std::to_string(seconds) + " s.");
    }
    return changed;
}

void EditorAgentPanelState::ReadLine(const std::string& line) {
    if (line.empty()) {
        return;
    }
    const EditorAgent* agent = Current();
    const EditorAgentOutputFormat format = agent != nullptr ? agent->output : EditorAgentOutputFormat::PlainText;
    if (format == EditorAgentOutputFormat::ClaudeStreamJson && line.front() == '{') {
        ReadClaudeEvent(line);
    } else if (format == EditorAgentOutputFormat::CodexJson && line.front() == '{') {
        ReadCodexEvent(line);
    } else if (format == EditorAgentOutputFormat::PlainText) {
        if (!entries_.empty() && entries_.back().kind == EditorAgentEntryKind::Agent) {
            entries_.back().text += "\n" + line;
        } else {
            Add(EditorAgentEntryKind::Agent, line);
        }
    } else if (line.find("Reading prompt from stdin") == std::string::npos) {
        Add(EditorAgentEntryKind::Info, Shorten(line));
    }
}

void EditorAgentPanelState::ReadClaudeEvent(const std::string& line) {
    JsonValue event;
    std::string error;
    if (!JsonValue::Parse(line, event, error)) {
        return;
    }
    const std::string& type = StringOf(event.Find("type"));
    if (type == "system" && StringOf(event.Find("subtype")) == "init") {
        session_ = StringOf(event.Find("session_id"));
        sessionAgent_ = Current() != nullptr ? Current()->Label() : std::string{};
        return;
    }
    const JsonValue* message = event.Find("message");
    const JsonValue* content = message != nullptr ? message->Find("content") : nullptr;
    if ((type == "assistant" || type == "user") && content != nullptr && content->GetKind() == JsonValue::Kind::Array) {
        for (std::size_t index = 0; index < content->Size(); ++index) {
            const JsonValue* part = content->At(index);
            const std::string& kind = StringOf(part->Find("type"));
            if (type == "assistant" && kind == "text" && !StringOf(part->Find("text")).empty()) {
                Add(EditorAgentEntryKind::Agent, WithoutMarkdown(StringOf(part->Find("text"))));
            } else if (type == "assistant" && kind == "tool_use") {
                Add(EditorAgentEntryKind::Tool, ToolLine(StringOf(part->Find("name")), part->Find("input")));
            } else if (type == "user" && kind == "tool_result") {
                const JsonValue* failed = part->Find("is_error");
                if (failed != nullptr && failed->AsBool()) {
                    const JsonValue* body = part->Find("content");
                    std::string text = StringOf(body);
                    if (text.empty() && body != nullptr && body->GetKind() == JsonValue::Kind::Array && body->Size() > 0U) {
                        text = StringOf(body->At(0)->Find("text"));
                    }
                    Add(EditorAgentEntryKind::Error, Shorten(text.empty() ? std::string{ "A tool failed." } : text));
                }
            }
        }
        return;
    }
    if (type == "result") {
        const JsonValue* failed = event.Find("is_error");
        if (failed != nullptr && failed->AsBool()) {
            Add(EditorAgentEntryKind::Error, Shorten(StringOf(event.Find("result"))));
            sawError_ = true;
        }
    }
}

void EditorAgentPanelState::ReadCodexEvent(const std::string& line) {
    JsonValue event;
    std::string error;
    if (!JsonValue::Parse(line, event, error)) {
        return;
    }
    const std::string& type = StringOf(event.Find("type"));
    if (type == "turn.failed" || type == "error") {
        const JsonValue* failure = event.Find("error");
        std::string text = failure != nullptr ? StringOf(failure->Find("message")) : StringOf(event.Find("message"));
        Add(EditorAgentEntryKind::Error, Shorten(text.empty() ? std::string{ "The agent reported an error." } : text));
        sawError_ = true;
        return;
    }
    if (type != "item.completed") {
        return;
    }
    const JsonValue* item = event.Find("item");
    if (item == nullptr) {
        return;
    }
    const std::string& kind = StringOf(item->Find("type"));
    if (kind == "agent_message") {
        Add(EditorAgentEntryKind::Agent, WithoutMarkdown(StringOf(item->Find("text"))));
    } else if (kind == "command_execution") {
        Add(EditorAgentEntryKind::Tool, "$ " + Shorten(StringOf(item->Find("command")), 120U));
    } else if (kind == "mcp_tool_call") {
        Add(EditorAgentEntryKind::Tool, StringOf(item->Find("tool")));
    } else if (kind == "file_change") {
        std::string files;
        if (const JsonValue* changes = item->Find("changes"); changes != nullptr && changes->GetKind() == JsonValue::Kind::Array) {
            for (std::size_t index = 0; index < changes->Size(); ++index) {
                files += (files.empty() ? "" : ", ") + std::filesystem::path{ StringOf(changes->At(index)->Find("path")) }.filename().string();
            }
        }
        Add(EditorAgentEntryKind::Tool, "edit  " + Shorten(files, 120U));
    } else if (kind == "error") {
        Add(EditorAgentEntryKind::Error, Shorten(StringOf(item->Find("message"))));
    }
}

void EditorAgentPanelState::ScrollBy(int pixels, int maximum) noexcept {
    scrollFromBottom_ = std::clamp(scrollFromBottom_ + pixels, 0, std::max(0, maximum));
}

} // namespace kb::editor
