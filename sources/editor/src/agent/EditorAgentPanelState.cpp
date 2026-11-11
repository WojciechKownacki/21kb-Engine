#include "agent/EditorAgentPanelState.hpp"

#include "engine/core/JsonValue.hpp"
#include "localization/EditorLocalization.hpp"

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

// A conversation's tab title: the start of its first request.
[[nodiscard]] std::string TitleFrom(const std::string& request) {
    std::string title = request.substr(0, request.find('\n'));
    if (title.size() > 32U) {
        std::size_t cut = 32U;
        while (cut > 0U && (static_cast<unsigned char>(title[cut]) & 0xC0U) == 0x80U) --cut;
        title = title.substr(0, cut) + "...";
    }
    return title;
}

[[nodiscard]] std::uint64_t Count(const JsonValue* value) {
    return value != nullptr ? static_cast<std::uint64_t>(std::max(0.0, value->AsNumber())) : 0U;
}

} // namespace

EditorAgentPanelState::EditorAgentPanelState() {
    NewConversation();
}

EditorAgentPanelState::~EditorAgentPanelState() = default;

const EditorAgent* EditorAgentPanelState::Current() const noexcept {
    return current_ >= 0 && current_ < static_cast<int>(agents_.size()) ? &agents_[static_cast<std::size_t>(current_)] : nullptr;
}

const EditorAgentConversation& EditorAgentPanelState::Conversation(int index) const noexcept {
    return *conversations_[static_cast<std::size_t>(std::clamp(index, 0, ConversationCount() - 1))];
}

void EditorAgentPanelState::RefreshAgents() {
    std::string remembered = Current() != nullptr ? Current()->Label() : std::string{};
    std::string model = model_;
    std::string effort = effort_;
    if (remembered.empty()) {
        std::ifstream file(SelectionFile(), std::ios::binary);
        std::getline(file, remembered);
        std::getline(file, model);
        std::getline(file, effort);
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
    // A remembered model or level the agent does not offer falls back to its default.
    const EditorAgent* agent = Current();
    model_ = agent != nullptr && std::ranges::find(agent->models, model) != agent->models.end() ? model : std::string{};
    effort_ = agent != nullptr && std::ranges::find(agent->efforts, effort) != agent->efforts.end() ? effort : std::string{};
    StartAccountCheck();
}

void EditorAgentPanelState::Prepare() {
    if (!prepared_) {
        // The first tab was made before the editor's language was chosen.
        for (const std::unique_ptr<EditorAgentConversation>& conversation : conversations_) {
            if (conversation->entries.empty()) {
                conversation->title = std::string{ EditorText("agent.newConversation", "New conversation") };
            }
        }
        RefreshAgents();
    }
}

void EditorAgentPanelState::SelectAgent(int index) {
    if (index < 0 || index >= static_cast<int>(agents_.size()) || index == current_) {
        return;
    }
    current_ = index;
    model_.clear();
    effort_.clear();
    RememberSelection();
    StartAccountCheck();
}

void EditorAgentPanelState::SelectModel(std::string model) {
    model_ = std::move(model);
    RememberSelection();
}

void EditorAgentPanelState::SelectEffort(std::string effort) {
    effort_ = std::move(effort);
    RememberSelection();
}

void EditorAgentPanelState::RememberSelection() const {
    const std::filesystem::path path = SelectionFile();
    if (path.empty() || Current() == nullptr) {
        return;
    }
    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    file << Current()->Label() << '\n' << model_ << '\n' << effort_ << '\n';
}

void EditorAgentPanelState::StartAccountCheck() {
    plan_.clear();
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
            plan_ = StringOf(status.Find("subscriptionType"));
            if (!plan_.empty()) {
                plan_[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(plan_[0])));
            }
        }
        return;
    }
    // Codex prints one sentence: "Logged in using ChatGPT" or "Not logged in".
    signedIn_ = text.find("Logged in") != std::string::npos && text.find("Not logged in") == std::string::npos;
    if (const std::size_t at = text.find("using "); signedIn_ && at != std::string::npos) {
        plan_ = text.substr(at + 6U);
        while (!plan_.empty() && (plan_.back() == '\n' || plan_.back() == '\r')) plan_.pop_back();
    }
}

void EditorAgentPanelState::NewConversation() {
    auto conversation = std::make_unique<EditorAgentConversation>();
    conversation->title = std::string{ EditorText("agent.newConversation", "New conversation") };
    conversations_.push_back(std::move(conversation));
    active_ = ConversationCount() - 1;
}

void EditorAgentPanelState::SelectConversation(int index) {
    if (index >= 0 && index < ConversationCount()) {
        active_ = index;
        pendingHighSurrogate_ = 0;
    }
}

void EditorAgentPanelState::CloseConversation(int index) {
    if (index < 0 || index >= ConversationCount()) {
        return;
    }
    if (EditorAgentConversation& closing = *conversations_[static_cast<std::size_t>(index)]; closing.run != nullptr && closing.running) {
        closing.run->Stop();
    }
    conversations_.erase(conversations_.begin() + index);
    if (conversations_.empty()) {
        NewConversation();
    }
    active_ = std::clamp(active_ > index ? active_ - 1 : active_, 0, ConversationCount() - 1);
}

void EditorAgentPanelState::FocusInput(bool focused) noexcept {
    inputFocused_ = focused;
    pendingHighSurrogate_ = 0;
}

void EditorAgentPanelState::TypeCharacter(wchar_t character) {
    std::string& draft = ActiveMutable().draft;
    if (character >= 0xD800 && character <= 0xDBFF) {
        pendingHighSurrogate_ = character;
        return;
    }
    if (character >= 0xDC00 && character <= 0xDFFF) {
        if (pendingHighSurrogate_ != 0) {
            const char32_t code = 0x10000U + ((static_cast<char32_t>(pendingHighSurrogate_) - 0xD800U) << 10U) +
                (static_cast<char32_t>(character) - 0xDC00U);
            AppendUtf8(draft, code);
        }
        pendingHighSurrogate_ = 0;
        return;
    }
    pendingHighSurrogate_ = 0;
    if (character >= 0x20) {
        AppendUtf8(draft, static_cast<char32_t>(character));
    }
}

void EditorAgentPanelState::InsertText(std::string_view text) {
    std::string& draft = ActiveMutable().draft;
    for (const char character : text) {
        if (character != '\r') {
            draft.push_back(character == '\t' ? ' ' : character);
        }
    }
}

void EditorAgentPanelState::SetInput(std::string text) {
    ActiveMutable().draft = std::move(text);
}

void EditorAgentPanelState::Backspace() {
    std::string& draft = ActiveMutable().draft;
    // One whole character, however many UTF-8 bytes it took.
    while (!draft.empty() && (static_cast<unsigned char>(draft.back()) & 0xC0U) == 0x80U) {
        draft.pop_back();
    }
    if (!draft.empty()) {
        draft.pop_back();
    }
}

bool EditorAgentPanelState::AnyRunning() const noexcept {
    return std::ranges::any_of(conversations_, [](const auto& conversation) { return conversation->running; });
}

bool EditorAgentPanelState::Send(const EditorAgentWorkspace& workspace) {
    EditorAgentConversation& conversation = ActiveMutable();
    const EditorAgent* agent = Current();
    std::string request = conversation.draft;
    while (!request.empty() && (request.back() == ' ' || request.back() == '\n')) request.pop_back();
    if (conversation.running || request.empty()) {
        return false;
    }
    answerDismissed_ = false;
    if (agent == nullptr) {
        Add(EditorAgentEntryKind::Error, "No agent is installed. Install Claude Code, Codex, Gemini CLI, Qwen Code or Grok, "
            "or add your own from the agent menu, then choose Refresh.");
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

    // The agent reads the limit with every request, so a mode holds for agents that have no switch for it.
    const std::string wire = (mode_ == EditorAgentMode::Plan
        ? std::string{ "Plan only. Do not change any file, scene or asset, and do not run anything that changes the project. Reply with the steps you would take.\n\n" }
        : mode_ == EditorAgentMode::Ask
            ? std::string{ "Answer only. Do not change any file, scene or asset, and do not run anything that changes the project.\n\n" }
            : std::string{}) + request;
    bool promptInArguments = false;
    const auto substitute = [&](const std::string& word) {
        std::string text = Replace(word, "{project}", workspace.project.generic_string());
        text = Replace(text, "{kbcli}", workspace.kbcli.generic_string());
        text = Replace(text, "{mcpconfig}", mcpConfig.generic_string());
        text = Replace(text, "{session}", conversation.session);
        text = Replace(text, "{model}", model_);
        text = Replace(text, "{effort}", effort_);
        text = Replace(text, "{permissionMode}", mode_ == EditorAgentMode::Agent ? "acceptEdits" : "plan");
        if (text.find("{prompt}") != std::string::npos) {
            promptInArguments = true;
            text = Replace(text, "{prompt}", wire);
        }
        return Widen(text);
    };
    // An agent that stays running gets the next request on its input, as long as it runs with the
    // same agent, model and reasoning level; otherwise it starts again and resumes the session.
    const bool persistent = !agent->requestTemplate.empty() && !promptInArguments;
    const std::string runKey = agent->Label() + "|" + model_ + "|" + effort_ + "|" + std::to_string(static_cast<int>(mode_));
    const std::string message = persistent ? Replace(agent->requestTemplate, "{request}", JsonValue::MakeString(wire).Dump()) : std::string{};
    if (persistent && conversation.run != nullptr && conversation.run->Alive() && conversation.runKey == runKey) {
        if (conversation.entries.empty()) {
            conversation.title = TitleFrom(request);
        }
        Add(EditorAgentEntryKind::User, request);
        conversation.draft.clear();
        conversation.scrollFromBottom = 0;
        conversation.sawError = false;
        conversation.turnEnded = false;
        conversation.project = workspace.project;
        ProjectStamp(conversation.project, conversation.projectFiles, conversation.projectNewest);
        if (conversation.run->Write(message)) {
            conversation.running = true;
            conversation.requestRun = true;
            conversation.started = std::chrono::steady_clock::now();
            return true;
        }
        conversation.entries.pop_back(); // it ended meanwhile: start it again below, with the request
    }
    if (conversation.run != nullptr && conversation.run->Alive()) {
        conversation.run->Stop();
    }

    std::vector<std::wstring> arguments;
    const bool resuming = !conversation.session.empty() && conversation.sessionAgent == agent->Label();
    const bool resumeCommand = resuming && !agent->resumeRunArguments.empty();
    for (const std::string& word : resumeCommand ? agent->resumeRunArguments : agent->runArguments) {
        arguments.push_back(substitute(word));
    }
    if (persistent) {
        for (const std::string& word : agent->persistentArguments) arguments.push_back(substitute(word));
    }
    if (!model_.empty()) {
        for (const std::string& word : agent->modelArguments) arguments.push_back(substitute(word));
    }
    if (!effort_.empty()) {
        for (const std::string& word : agent->effortArguments) arguments.push_back(substitute(word));
    }
    if (resuming && !resumeCommand) {
        for (const std::string& word : agent->resumeArguments) arguments.push_back(substitute(word));
    }
    // A batch script (.cmd, .bat) runs through cmd.exe, which reads % ^ & | < > " in its command
    // line: a request in the arguments could be mangled or run commands, so such an agent must take
    // the request on standard input instead.
    const std::wstring extension = agent->executable.extension().wstring();
    if (promptInArguments && (_wcsicmp(extension.c_str(), L".cmd") == 0 || _wcsicmp(extension.c_str(), L".bat") == 0)) {
        Add(EditorAgentEntryKind::Error, agent->Label() + " is a batch script that takes the request on its command line, "
            "where cmd.exe would change it. Give it the request on standard input (leave {prompt} out of its arguments in Agents.json).");
        return false;
    }

    // kb_cli first on PATH, so an agent that runs commands itself finds the engine's tool too.
    std::vector<std::pair<std::wstring, std::wstring>> environment = agent->environment;
#if defined(_WIN32)
    std::wstring path(32767, L'\0');
    path.resize(GetEnvironmentVariableW(L"PATH", path.data(), static_cast<DWORD>(path.size())));
    environment.emplace_back(L"PATH", workspace.kbcli.parent_path().wstring() + L";" + path);
#endif

    if (conversation.entries.empty()) {
        conversation.title = TitleFrom(request);
    }
    Add(EditorAgentEntryKind::User, request);
    conversation.draft.clear();
    conversation.scrollFromBottom = 0;
    conversation.sawError = false;
    conversation.project = workspace.project;
    ProjectStamp(conversation.project, conversation.projectFiles, conversation.projectNewest);
    conversation.turnEnded = false;
    conversation.run = std::make_unique<EditorAgentProcess>();
    std::string error;
    if (!conversation.run->Start(agent->executable, arguments, workspace.project, environment,
            persistent ? message : promptInArguments ? std::string{} : wire, error, persistent)) {
        Add(EditorAgentEntryKind::Error, agent->Label() + ": " + error + ".");
        return false;
    }
    conversation.runKey = persistent ? runKey : std::string{};
    conversation.runFormat = agent->output;
    conversation.running = true;
    conversation.requestRun = true;
    conversation.started = std::chrono::steady_clock::now();
    return true;
}

bool EditorAgentPanelState::ConnectEngineTools(const EditorAgentWorkspace& workspace) {
    EditorAgentConversation& conversation = ActiveMutable();
    const EditorAgent* agent = Current();
    if (agent == nullptr || agent->connectArguments.empty() || conversation.running) {
        return false;
    }
    std::vector<std::wstring> arguments;
    for (const std::string& word : agent->connectArguments) {
        std::string text = Replace(word, "{project}", workspace.project.generic_string());
        arguments.push_back(Widen(Replace(text, "{kbcli}", workspace.kbcli.generic_string())));
    }
    Add(EditorAgentEntryKind::Info, "Connecting the engine's tools to " + agent->Label() + "...");
    conversation.run = std::make_unique<EditorAgentProcess>();
    std::string error;
    if (!conversation.run->Start(agent->executable, arguments, workspace.project, agent->environment, {}, error)) {
        Add(EditorAgentEntryKind::Error, error + ".");
        return false;
    }
    conversation.runFormat = EditorAgentOutputFormat::PlainText;
    conversation.running = true;
    conversation.requestRun = false;
    conversation.sawError = false;
    conversation.started = std::chrono::steady_clock::now();
    return true;
}

void EditorAgentPanelState::Stop() {
    EditorAgentConversation& conversation = ActiveMutable();
    if (conversation.run != nullptr && conversation.running) {
        conversation.run->Stop();
        Add(EditorAgentEntryKind::Info, "Stopped.");
        conversation.sawError = true;
    }
}

void EditorAgentPanelState::Add(EditorAgentEntryKind kind, std::string text) {
    ActiveMutable().entries.push_back(EditorAgentEntry{ kind, std::move(text) });
}

bool EditorAgentPanelState::Pump(bool& projectChanged) {
    projectChanged = false;
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
    for (const auto& owned : conversations_) {
        EditorAgentConversation& conversation = *owned;
        if (conversation.run == nullptr || !conversation.running) {
            continue;
        }
        for (const std::string& line : conversation.run->TakeLines()) {
            ReadLine(conversation, line);
            changed = true;
        }
        // A turn ends when the agent exits, or, for one that stays running, at its result event.
        const std::optional<int> code = conversation.turnEnded ? std::optional<int>{ 0 } : conversation.run->ExitCode();
        if (!code.has_value()) {
            continue;
        }
        conversation.turnEnded = false;
        conversation.running = false;
        changed = true;
        if (*code != 0 && !conversation.sawError) {
            conversation.entries.push_back({ EditorAgentEntryKind::Error, "The agent stopped with exit code " + std::to_string(*code) + "." });
        }
        const auto seconds = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - conversation.started).count();
        conversation.entries.push_back({ EditorAgentEntryKind::Info, "Finished in " + std::to_string(seconds) + " s." });
        if (conversation.requestRun) {
            std::uintmax_t files = 0U;
            std::filesystem::file_time_type newest{};
            ProjectStamp(conversation.project, files, newest);
            projectChanged = projectChanged || files != conversation.projectFiles || newest != conversation.projectNewest;
        }
    }
    return changed;
}

void EditorAgentPanelState::ReadLine(EditorAgentConversation& conversation, const std::string& line) {
    if (line.empty()) {
        return;
    }
    if (conversation.runFormat == EditorAgentOutputFormat::ClaudeStreamJson && line.front() == '{') {
        ReadClaudeEvent(conversation, line);
    } else if (conversation.runFormat == EditorAgentOutputFormat::CodexJson && line.front() == '{') {
        ReadCodexEvent(conversation, line);
    } else if (conversation.runFormat == EditorAgentOutputFormat::PlainText) {
        if (!conversation.entries.empty() && conversation.entries.back().kind == EditorAgentEntryKind::Agent) {
            conversation.entries.back().text += "\n" + line;
        } else {
            conversation.entries.push_back({ EditorAgentEntryKind::Agent, line });
        }
    } else if (line.find("Reading prompt from stdin") == std::string::npos) {
        conversation.entries.push_back({ EditorAgentEntryKind::Info, Shorten(line) });
    }
}

void EditorAgentPanelState::ReadClaudeEvent(EditorAgentConversation& conversation, const std::string& line) {
    JsonValue event;
    std::string error;
    if (!JsonValue::Parse(line, event, error)) {
        return;
    }
    const std::string& type = StringOf(event.Find("type"));
    if (type == "system" && StringOf(event.Find("subtype")) == "init") {
        conversation.session = StringOf(event.Find("session_id"));
        conversation.sessionAgent = Current() != nullptr ? Current()->Label() : std::string{};
        return;
    }
    const JsonValue* message = event.Find("message");
    const JsonValue* content = message != nullptr ? message->Find("content") : nullptr;
    // Each step of the agent reports what it sent the model; the latest step's prompt and reply
    // are what the conversation now fills of the context window.
    if (const JsonValue* usage = type == "assistant" && message != nullptr ? message->Find("usage") : nullptr; usage != nullptr) {
        const std::uint64_t filled = Count(usage->Find("input_tokens")) + Count(usage->Find("cache_read_input_tokens")) +
            Count(usage->Find("cache_creation_input_tokens")) + Count(usage->Find("output_tokens"));
        if (filled > 0U) {
            conversation.usage.contextTokens = filled;
        }
    }
    if ((type == "assistant" || type == "user") && content != nullptr && content->GetKind() == JsonValue::Kind::Array) {
        for (std::size_t index = 0; index < content->Size(); ++index) {
            const JsonValue* part = content->At(index);
            const std::string& kind = StringOf(part->Find("type"));
            if (type == "assistant" && kind == "text" && !StringOf(part->Find("text")).empty()) {
                conversation.entries.push_back({ EditorAgentEntryKind::Agent, WithoutMarkdown(StringOf(part->Find("text"))) });
            } else if (type == "assistant" && kind == "tool_use") {
                conversation.entries.push_back({ EditorAgentEntryKind::Tool, ToolLine(StringOf(part->Find("name")), part->Find("input")) });
            } else if (type == "user" && kind == "tool_result") {
                const JsonValue* failed = part->Find("is_error");
                if (failed != nullptr && failed->AsBool()) {
                    const JsonValue* body = part->Find("content");
                    std::string text = StringOf(body);
                    if (text.empty() && body != nullptr && body->GetKind() == JsonValue::Kind::Array && body->Size() > 0U) {
                        text = StringOf(body->At(0)->Find("text"));
                    }
                    // A refused or failed tool call is part of the agent's own work - it reads the
                    // reason and tries another way - so it is a quiet note, not an error of the request.
                    conversation.entries.push_back({ EditorAgentEntryKind::Info, Shorten(text.empty() ? std::string{ "A tool failed." } : text) });
                }
            }
        }
        return;
    }
    if (type == "result") {
        conversation.turnEnded = true;
        const JsonValue* failed = event.Find("is_error");
        if (failed != nullptr && failed->AsBool()) {
            conversation.entries.push_back({ EditorAgentEntryKind::Error, Shorten(StringOf(event.Find("result"))) });
            conversation.sawError = true;
        }
        // The run's totals: tokens the model processed over all its steps (every step reads the whole
        // conversation again, mostly from cache) and their cost. The context fill comes from the steps.
        if (const JsonValue* models = event.Find("modelUsage"); models != nullptr && models->GetKind() == JsonValue::Kind::Object) {
            for (std::size_t index = 0; index < models->Size(); ++index) {
                const JsonValue* model = models->At(index);
                conversation.usage.inputTokens += Count(model->Find("inputTokens")) + Count(model->Find("cacheReadInputTokens")) +
                    Count(model->Find("cacheCreationInputTokens"));
                conversation.usage.outputTokens += Count(model->Find("outputTokens"));
                conversation.usage.costUsd += model->Find("costUSD") != nullptr ? model->Find("costUSD")->AsNumber() : 0.0;
                if (index == 0U) {
                    conversation.usage.contextWindow = Count(model->Find("contextWindow"));
                }
            }
        }
    }
}

void EditorAgentPanelState::ReadCodexEvent(EditorAgentConversation& conversation, const std::string& line) {
    JsonValue event;
    std::string error;
    if (!JsonValue::Parse(line, event, error)) {
        return;
    }
    const std::string& type = StringOf(event.Find("type"));
    // The thread a follow-up request continues (codex exec resume <thread>).
    if (type == "thread.started") {
        conversation.session = StringOf(event.Find("thread_id"));
        conversation.sessionAgent = Current() != nullptr ? Current()->Label() : std::string{};
        return;
    }
    if (type == "turn.failed" || type == "error") {
        const JsonValue* failure = event.Find("error");
        std::string text = failure != nullptr ? StringOf(failure->Find("message")) : StringOf(event.Find("message"));
        conversation.entries.push_back({ EditorAgentEntryKind::Error, Shorten(text.empty() ? std::string{ "The agent reported an error." } : text) });
        conversation.sawError = true;
        return;
    }
    if (type == "turn.completed") {
        if (const JsonValue* usage = event.Find("usage"); usage != nullptr) {
            const std::uint64_t input = Count(usage->Find("input_tokens"));
            conversation.usage.inputTokens += input;
            conversation.usage.outputTokens += Count(usage->Find("output_tokens"));
            conversation.usage.contextTokens = input + Count(usage->Find("output_tokens"));
        }
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
        conversation.entries.push_back({ EditorAgentEntryKind::Agent, WithoutMarkdown(StringOf(item->Find("text"))) });
    } else if (kind == "command_execution") {
        conversation.entries.push_back({ EditorAgentEntryKind::Tool, "$ " + Shorten(StringOf(item->Find("command")), 120U) });
    } else if (kind == "mcp_tool_call") {
        conversation.entries.push_back({ EditorAgentEntryKind::Tool, StringOf(item->Find("tool")) });
    } else if (kind == "file_change") {
        std::string files;
        if (const JsonValue* changes = item->Find("changes"); changes != nullptr && changes->GetKind() == JsonValue::Kind::Array) {
            for (std::size_t index = 0; index < changes->Size(); ++index) {
                files += (files.empty() ? "" : ", ") + std::filesystem::path{ StringOf(changes->At(index)->Find("path")) }.filename().string();
            }
        }
        conversation.entries.push_back({ EditorAgentEntryKind::Tool, "edit  " + Shorten(files, 120U) });
    } else if (kind == "error") {
        conversation.entries.push_back({ EditorAgentEntryKind::Error, Shorten(StringOf(item->Find("message"))) });
    }
}

void EditorAgentPanelState::ScrollBy(int pixels, int maximum) noexcept {
    int& scroll = ActiveMutable().scrollFromBottom;
    scroll = std::clamp(scroll + pixels, 0, std::max(0, maximum));
}

} // namespace kb::editor
