#include "app/agent/EditorAgentPanelController.hpp"

#if defined(_WIN32)
#include "app/EditorTextInputShortcuts.hpp"
#include "engine/platform/win32/Win32ClipboardText.hpp"
#include "engine/project/AgentRequestContext.hpp"
#include "engine/scene/SceneEntities.hpp"
#include "engine/script/ScriptAgentProjectFiles.hpp"
#include "engine/script/ScriptApiCatalog.hpp"
#include "engine/script/ScriptRuntimeHost.hpp"
#include "platform/win32/EditorFileDialogFilter.hpp"
#include "platform/win32/EditorTextEntryDialog.hpp"
#include "localization/EditorLocalization.hpp"
#include "project/EditorProjectPaths.hpp"
#include "rendering/AgentBarRenderer.hpp"
#include "rendering/AgentPanelRenderer.hpp"
#include "rendering/HeroIconGdiplusRuntime.hpp"
#include "scene/EditorSceneContext.hpp"

#include <CommDlg.h>
#include <objidl.h>
#include <propidl.h>
#include <gdiplus.h>
#include <shellapi.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <cwctype>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace kb::editor {
namespace {

constexpr UINT kRefreshCommand = 1U;
constexpr UINT kEditAgentsCommand = 2U;
constexpr UINT kFirstAgentCommand = 100U;
constexpr UINT kFirstChoiceCommand = 1000U;
constexpr UINT kFirstEffortCommand = 2000U;
constexpr UINT kFirstModelCommand = 3000U;
constexpr UINT kModelsPerAgent = 64U;

[[nodiscard]] std::wstring Widen(std::string_view text) {
    if (text.empty()) return {};
    const int length = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
    std::wstring wide(static_cast<std::size_t>(std::max(length, 0)), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), wide.data(), length);
    return wide;
}

[[nodiscard]] bool ControlDown() noexcept {
    return (GetKeyState(VK_CONTROL) & 0x8000) != 0;
}

[[nodiscard]] std::string Lowered(std::string_view text) {
    std::string lowered{ text };
    for (char& character : lowered) {
        character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
    }
    return lowered;
}

[[nodiscard]] bool ShiftDown() noexcept {
    return (GetKeyState(VK_SHIFT) & 0x8000) != 0;
}

[[nodiscard]] bool IsPicture(const std::filesystem::path& file) {
    std::wstring extension = file.extension().wstring();
    for (wchar_t& character : extension) character = static_cast<wchar_t>(towlower(character));
    return extension == L".png" || extension == L".jpg" || extension == L".jpeg" || extension == L".bmp";
}

// Where pasted screenshots are kept: inside the project, where the agent may read them.
[[nodiscard]] std::filesystem::path PicturesFolder() {
    return EditorProjectPaths::ProjectRoot() / "Saved" / "Agent" / "Images";
}

[[nodiscard]] std::optional<CLSID> PngEncoder() {
    UINT count = 0U;
    UINT bytes = 0U;
    if (Gdiplus::GetImageEncodersSize(&count, &bytes) != Gdiplus::Ok || bytes == 0U) return std::nullopt;
    std::vector<std::byte> buffer(bytes);
    auto* encoders = reinterpret_cast<Gdiplus::ImageCodecInfo*>(buffer.data());
    if (Gdiplus::GetImageEncoders(count, bytes, encoders) != Gdiplus::Ok) return std::nullopt;
    for (UINT index = 0; index < count; ++index) {
        if (std::wstring_view{ encoders[index].MimeType } == L"image/png") return encoders[index].Clsid;
    }
    return std::nullopt;
}

// A bitmap on the clipboard (a screenshot, a copied picture) written as a PNG; nothing when the
// clipboard holds none.
[[nodiscard]] std::optional<std::filesystem::path> SaveClipboardPicture(HWND owner) {
    if (IsClipboardFormatAvailable(CF_DIB) == FALSE || OpenClipboard(owner) == FALSE) return std::nullopt;
    std::vector<std::byte> dib;
    if (HANDLE handle = GetClipboardData(CF_DIB); handle != nullptr) {
        if (const void* data = GlobalLock(handle); data != nullptr) {
            dib.resize(GlobalSize(handle));
            std::memcpy(dib.data(), data, dib.size());
            GlobalUnlock(handle);
        }
    }
    CloseClipboard();
    if (dib.size() < sizeof(BITMAPINFOHEADER)) return std::nullopt;
    const auto* info = reinterpret_cast<const BITMAPINFO*>(dib.data());
    const BITMAPINFOHEADER& header = info->bmiHeader;
    // The pixels follow the header, its colour masks and its palette.
    std::size_t offset = header.biSize;
    if (header.biSize == sizeof(BITMAPINFOHEADER) && header.biCompression == BI_BITFIELDS) offset += 3U * sizeof(DWORD);
    const std::size_t colours = header.biClrUsed != 0U ? header.biClrUsed : header.biBitCount <= 8U ? (1U << header.biBitCount) : 0U;
    offset += colours * sizeof(RGBQUAD);
    if (offset >= dib.size() || header.biWidth <= 0 || header.biHeight == 0) return std::nullopt;
    HeroIconGdiplusRuntime::EnsureStarted();
    const std::optional<CLSID> png = PngEncoder();
    if (!png.has_value()) return std::nullopt;
    Gdiplus::Bitmap bitmap(info, dib.data() + offset);
    if (bitmap.GetLastStatus() != Gdiplus::Ok) return std::nullopt;
    std::error_code error;
    std::filesystem::create_directories(PicturesFolder(), error);
    SYSTEMTIME now{};
    GetLocalTime(&now);
    wchar_t name[64]{};
    std::swprintf(name, 64, L"Screenshot-%04u%02u%02u-%02u%02u%02u-%03u.png", now.wYear, now.wMonth, now.wDay, now.wHour, now.wMinute, now.wSecond,
        now.wMilliseconds);
    const std::filesystem::path file = PicturesFolder() / name;
    if (bitmap.Save(file.c_str(), &*png, nullptr) != Gdiplus::Ok) return std::nullopt;
    return file;
}

// Image files copied in Explorer.
[[nodiscard]] std::vector<std::filesystem::path> ClipboardPictureFiles(HWND owner) {
    std::vector<std::filesystem::path> files;
    if (IsClipboardFormatAvailable(CF_HDROP) == FALSE || OpenClipboard(owner) == FALSE) return files;
    if (HANDLE handle = GetClipboardData(CF_HDROP); handle != nullptr) {
        const auto drop = static_cast<HDROP>(handle);
        const UINT count = DragQueryFileW(drop, 0xFFFFFFFFU, nullptr, 0U);
        for (UINT index = 0; index < count; ++index) {
            std::wstring path(static_cast<std::size_t>(DragQueryFileW(drop, index, nullptr, 0U)) + 1U, L'\0');
            path.resize(DragQueryFileW(drop, index, path.data(), static_cast<UINT>(path.size())));
            if (IsPicture(path)) files.emplace_back(path);
        }
    }
    CloseClipboard();
    return files;
}

[[nodiscard]] std::string ConversationLabel(const EditorAgentConversation& conversation) {
    return conversation.title.empty() ? std::string{ EditorText("agent.newConversation", "New conversation") } : conversation.title;
}

// A small popup of the given rows under `anchor`; the chosen row, or nothing.
[[nodiscard]] std::optional<std::size_t> PickFromMenu(HWND owner, const RECT& anchor, const std::vector<std::string>& rows, int checked) {
    HMENU menu = CreatePopupMenu();
    if (menu == nullptr) {
        return std::nullopt;
    }
    for (std::size_t index = 0; index < rows.size(); ++index) {
        AppendMenuW(menu, MF_STRING | (static_cast<int>(index) == checked ? MF_CHECKED : 0U), kFirstChoiceCommand + static_cast<UINT>(index), Widen(rows[index]).c_str());
    }
    POINT point{ anchor.left, anchor.bottom };
    ClientToScreen(owner, &point);
    const int chosen = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_LEFTALIGN | TPM_TOPALIGN | TPM_NONOTIFY, point.x, point.y, 0, owner, nullptr);
    DestroyMenu(menu);
    if (chosen < static_cast<int>(kFirstChoiceCommand)) {
        return std::nullopt;
    }
    return static_cast<std::size_t>(chosen) - kFirstChoiceCommand;
}

// The engine's own folders an agent may not read: beside the editor the checkout is found as the SDK finds it (the folder that holds
// the SDK headers), and everything in it is hidden but its documentation, its build folder (the SDK libraries) and the headers. A
// folder the project lies in or holds is never hidden.
[[nodiscard]] bool Contains(const std::filesystem::path& outer, const std::filesystem::path& inner) {
    const std::filesystem::path relative = inner.lexically_relative(outer);
    return !relative.empty() && *relative.begin() != "..";
}

[[nodiscard]] std::vector<std::filesystem::path> HiddenEngineFolders(const std::filesystem::path& kbcli, const std::filesystem::path& project) {
    namespace fs = std::filesystem;
    std::error_code error;
    fs::path root;
    for (fs::path probe = kbcli.parent_path(); !probe.empty() && probe != probe.parent_path(); probe = probe.parent_path()) {
        if (fs::is_regular_file(probe / "sources" / "engine" / "include" / "engine" / "script" / "NativeScriptPlugin.hpp", error)) {
            root = probe;
            break;
        }
    }
    std::vector<fs::path> hidden;
    if (root.empty()) return hidden;
    const fs::path where = fs::weakly_canonical(project, error);
    const auto addChildren = [&](const fs::path& folder, std::initializer_list<const char*> kept) {
        for (fs::directory_iterator it(folder, error), end; !error && it != end; it.increment(error)) {
            const fs::path& child = it->path();
            const std::string name = child.filename().string();
            if (!it->is_directory(error) || std::ranges::any_of(kept, [&name](const char* keep) { return name == keep; })) continue;
            const fs::path resolved = fs::weakly_canonical(child, error);
            if (Contains(resolved, where) || Contains(where, resolved)) continue;
            hidden.push_back(child);
        }
    };
    addChildren(root, { "docs", "build", "sources" });
    addChildren(root / "sources", { "engine" });
    addChildren(root / "sources" / "engine", { "include" });
    return hidden;
}

} // namespace

EditorAgentWorkspace EditorAgentPanelController::Workspace() {
    wchar_t module[MAX_PATH]{};
    GetModuleFileNameW(nullptr, module, MAX_PATH);
    EditorAgentWorkspace workspace{
        .project = EditorProjectPaths::ProjectRoot(),
        .kbcli = std::filesystem::path{ module }.parent_path() / "kb_cli.exe",
    };
    workspace.hiddenFolders = HiddenEngineFolders(workspace.kbcli, workspace.project);
    return workspace;
}

void EditorAgentPanelController::ShowAgentMenu(HWND owner, const RECT& anchor) {
    EditorAgentPanelState& agent = sceneContext_.AgentPanel();
    HMENU menu = CreatePopupMenu();
    if (menu == nullptr) {
        return;
    }
    const std::vector<EditorAgent>& agents = agent.Agents();
    for (std::size_t index = 0; index < agents.size(); ++index) {
        const UINT flags = MF_STRING | (static_cast<int>(index) == agent.CurrentIndex() ? MF_CHECKED : 0U);
        AppendMenuW(menu, flags, kFirstAgentCommand + static_cast<UINT>(index), Widen(agents[index].Label()).c_str());
    }
    if (!agents.empty()) {
        AppendMenuW(menu, MF_SEPARATOR, 0U, nullptr);
    }
    AppendMenuW(menu, MF_STRING, kRefreshCommand, Widen(EditorText("agent.refresh", "Refresh (find installed agents again)")).c_str());
    AppendMenuW(menu, MF_STRING, kEditAgentsCommand, Widen(EditorText("agent.editAgents", "Add your own agent...")).c_str());
    POINT point{ anchor.left, anchor.bottom };
    ClientToScreen(owner, &point);
    const int chosen = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_LEFTALIGN | TPM_TOPALIGN | TPM_NONOTIFY, point.x, point.y, 0, owner, nullptr);
    DestroyMenu(menu);
    if (chosen <= 0) {
        return;
    }
    const UINT command = static_cast<UINT>(chosen);
    if (command == kRefreshCommand) {
        agent.RefreshAgents();
    } else if (command == kEditAgentsCommand) {
        // The user's own agents live in a small JSON file with an example of its format.
        if (EnsureEditorAgentsFile()) {
            ShellExecuteW(owner, L"open", L"notepad.exe", EditorAgentsFile().wstring().c_str(), nullptr, SW_SHOWNORMAL);
            agent.Add(EditorAgentEntryKind::Info, "Opened " + EditorAgentsFile().string() +
                ". Add an agent to the \"agents\" list as in the example, save, then choose Refresh.");
        }
    } else if (command >= kFirstAgentCommand) {
        agent.SelectAgent(static_cast<int>(command - kFirstAgentCommand));
    }
}

void EditorAgentPanelController::ShowModeMenu(HWND owner, const RECT& anchor) {
    EditorAgentPanelState& agent = sceneContext_.AgentPanel();
    const std::vector<std::string> rows{
        std::string{ EditorText("agent.mode.agentRow", "Agent: changes the project") },
        std::string{ EditorText("agent.mode.planRow", "Plan: only says what it would do") },
        std::string{ EditorText("agent.mode.askRow", "Ask: only answers") },
    };
    if (const std::optional<std::size_t> chosen = PickFromMenu(owner, anchor, rows, static_cast<int>(agent.Mode()))) {
        agent.SelectMode(static_cast<EditorAgentMode>(*chosen));
    }
}

void EditorAgentPanelController::AttachFile(HWND owner) {
    std::array<wchar_t, 32768> buffer{};
    const std::wstring filter = EditorFileDialogFilter({ { EditorText("fileDialog.allFiles", "All files (*.*)"), "*.*" } });
    const std::wstring title = Widen(EditorText("agent.attachTitle", "Attach a file to the request"));
    const std::wstring directory = EditorProjectPaths::AssetsRoot().wstring();
    OPENFILENAMEW dialog{};
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = owner;
    dialog.lpstrFilter = filter.c_str();
    dialog.lpstrFile = buffer.data();
    dialog.nMaxFile = static_cast<DWORD>(buffer.size());
    dialog.lpstrInitialDir = directory.c_str();
    dialog.lpstrTitle = title.c_str();
    dialog.Flags = OFN_EXPLORER | OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (GetOpenFileNameW(&dialog) == FALSE) {
        return;
    }
    std::filesystem::path file{ buffer.data() };
    EditorAgentPanelState& agent = sceneContext_.AgentPanel();
    if (IsPicture(file)) {
        agent.Attach(file);
        agent.FocusInput(true);
        return;
    }
    // The agent works in the project folder, so a file inside it is named from there.
    std::error_code error;
    const std::filesystem::path relative = std::filesystem::relative(file, EditorProjectPaths::ProjectRoot(), error);
    if (!error && !relative.empty() && *relative.begin() != "..") {
        file = relative;
    }
    const std::u8string text = file.generic_u8string();
    agent.InsertText("\"" + std::string{ reinterpret_cast<const char*>(text.data()), text.size() } + "\" ");
    agent.FocusInput(true);
}

void EditorAgentPanelController::ShowConversations(HWND owner, const RECT& anchor) {
    // Every conversation of this project, newest first: the open ones are ticked, a closed one
    // opens again as a tab.
    EditorAgentPanelState& agent = sceneContext_.AgentPanel();
    agent.Prepare();
    const std::vector<EditorAgentSavedConversation> saved = agent.SavedConversations();
    std::vector<std::string> rows;
    int checked = -1;
    const std::string& active = agent.Active().id;
    for (std::size_t index = 0; index < saved.size(); ++index) {
        rows.push_back((saved[index].open ? std::string{} : std::string{ "    " }) +
            (saved[index].title.empty() ? std::string{ EditorText("agent.newConversation", "New conversation") } : saved[index].title));
        if (saved[index].id == active) checked = static_cast<int>(index);
    }
    if (rows.empty()) {
        rows.push_back(ConversationLabel(agent.Active()));
        checked = 0;
    }
    if (const std::optional<std::size_t> chosen = PickFromMenu(owner, anchor, rows, checked); chosen.has_value() && *chosen < saved.size()) {
        agent.OpenSaved(saved[*chosen].id);
    }
}

void EditorAgentPanelController::SearchConversations(HWND owner, const RECT& anchor) {
    const std::optional<std::string> query = EditorTextEntryDialog::Show(owner, {
        .title = std::string{ EditorText("agent.searchTitle", "Find in Conversations") },
        .label = std::string{ EditorText("agent.searchLabel", "Text to find") },
        .hint = std::string{ EditorText("agent.searchHint", "Looks through what you and the agent wrote.") },
        .acceptLabel = std::string{ EditorText("agent.searchAccept", "Find") },
        .icon = HeroIconKind::MagnifyingGlass,
    });
    if (!query.has_value() || query->empty()) {
        return;
    }
    EditorAgentPanelState& agent = sceneContext_.AgentPanel();
    const std::string needle = Lowered(*query);
    std::vector<int> found;
    std::vector<std::string> rows;
    for (int index = 0; index < agent.ConversationCount(); ++index) {
        const EditorAgentConversation& conversation = agent.Conversation(index);
        bool matches = Lowered(conversation.title).find(needle) != std::string::npos;
        for (const EditorAgentEntry& entry : conversation.entries) {
            matches = matches || Lowered(entry.text).find(needle) != std::string::npos;
        }
        if (matches) {
            found.push_back(index);
            rows.push_back(ConversationLabel(conversation));
        }
    }
    if (found.empty()) {
        agent.Add(EditorAgentEntryKind::Info, EditorLocalization::Format("agent.searchNothing", "No conversation mentions \"{text}\".", { { "text", *query } }));
        return;
    }
    if (const std::optional<std::size_t> chosen = PickFromMenu(owner, anchor, rows, -1)) {
        agent.SelectConversation(found[*chosen]);
    }
}

bool EditorAgentPanelController::HandlePointerDown(HWND owner, const RECT& content, int x, int y) {
    EditorAgentPanelState& agent = sceneContext_.AgentPanel();
    agent.Prepare();
    const AgentPanelHit hit = AgentPanelRenderer::HitTest(content, agent, x, y);
    agent.FocusInput(hit.kind == AgentPanelHitKind::Input || hit.kind == AgentPanelHitKind::Suggestion ||
        hit.kind == AgentPanelHitKind::NewTab || hit.kind == AgentPanelHitKind::Attachment || hit.kind == AgentPanelHitKind::PlanEdit);
    const AgentPanelLayout layout = AgentPanelRenderer::Resolve(content, agent);
    switch (hit.kind) {
    case AgentPanelHitKind::Tab:
        agent.SelectConversation(hit.index);
        return true;
    case AgentPanelHitKind::TabClose:
        agent.CloseConversation(hit.index);
        return true;
    case AgentPanelHitKind::NewTab:
        agent.NewConversation();
        return true;
    case AgentPanelHitKind::Undock:
        agent.RequestDock(EditorAgentPanelState::DockRequest::Undock);
        return true;
    case AgentPanelHitKind::AgentMenu:
        ShowAgentMenu(owner, layout.agentMenu);
        return true;
    case AgentPanelHitKind::ModeMenu:
        ShowModeMenu(owner, layout.modeMenu);
        return true;
    case AgentPanelHitKind::Attach:
        AttachFile(owner);
        return true;
    case AgentPanelHitKind::Chats:
        ShowConversations(owner, layout.chats);
        return true;
    case AgentPanelHitKind::Search:
        SearchConversations(owner, layout.search);
        return true;
    case AgentPanelHitKind::ModelMenu:
        ShowChoiceMenu(owner, layout.modelMenu, true);
        return true;
    case AgentPanelHitKind::EffortMenu:
        ShowChoiceMenu(owner, layout.effortMenu, false);
        return true;
    case AgentPanelHitKind::SignInHint: {
        // Signing in opens the agent's own sign-in in a window of its own (it continues in the
        // browser); the panel sees the account by itself once that is done.
        const std::wstring command = L"/k " + Widen(AgentPanelRenderer::SignInCommand(agent));
        if (reinterpret_cast<INT_PTR>(ShellExecuteW(owner, L"open", L"cmd.exe", command.c_str(), nullptr, SW_SHOWNORMAL)) > 32) {
            agent.Add(EditorAgentEntryKind::Info, std::string{ EditorText("agent.signInOpened",
                "A sign-in window opened. Sign in there (it continues in your browser); this panel notices by itself when you are done.") });
        }
        return true;
    }
    case AgentPanelHitKind::ConnectTools:
        return agent.ConnectEngineTools(Workspace());
    case AgentPanelHitKind::Suggestion:
        agent.SetInput(std::string{ AgentPanelRenderer::Suggestion(hit.index) });
        return true;
    case AgentPanelHitKind::Send:
        return SendOrStop();
    case AgentPanelHitKind::CardButton:
        return PressCardButton(hit.index);
    case AgentPanelHitKind::PlanToggle:
        agent.TogglePlanStep(hit.index);
        return true;
    case AgentPanelHitKind::PlanEdit:
        agent.EditPlanStep(hit.index);
        return true;
    case AgentPanelHitKind::PlanUp:
        agent.MovePlanStep(hit.index, -1);
        return true;
    case AgentPanelHitKind::PlanDown:
        agent.MovePlanStep(hit.index, 1);
        return true;
    case AgentPanelHitKind::Input:
        // The caret goes where you click; Shift+click selects up to there.
        agent.PlaceCaret(AgentPanelRenderer::InputOffset(content, agent, x, y), ShiftDown());
        return true;
    case AgentPanelHitKind::Attachment:
        agent.RemoveAttachment(static_cast<std::size_t>(hit.index));
        return true;
    case AgentPanelHitKind::ReviewKeep:
        agent.KeepChanges();
        return true;
    case AgentPanelHitKind::ReviewUndoAll:
        agent.UndoChanges();
        return true;
    case AgentPanelHitKind::ReviewUndoFile:
        agent.UndoChanges(hit.index);
        return true;
    case AgentPanelHitKind::Picture: {
        // A picture opens full size in the system's viewer.
        const EditorAgentEntry& entry = agent.Active().entries[static_cast<std::size_t>(hit.index)];
        const std::filesystem::path& file = hit.item >= 0 ? entry.images[static_cast<std::size_t>(hit.item)] : entry.image;
        ShellExecuteW(owner, L"open", file.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        return true;
    }
    case AgentPanelHitKind::None:
    default:
        return false;
    }
}

void EditorAgentPanelController::ShowChoiceMenu(HWND owner, const RECT& anchor, bool models) {
    EditorAgentPanelState& agent = sceneContext_.AgentPanel();
    const EditorAgent* current = agent.Current();
    if (current == nullptr) {
        return;
    }
    const std::string& selected = models ? agent.Model() : agent.Effort();
    // What each item hands back and what it shows: a model's exact command-line name with its shown name, or a level with
    // its own first letter capitalized.
    std::vector<std::string> values;
    std::vector<std::wstring> labels;
    if (models) {
        for (const EditorAgentModel& model : current->models) {
            values.push_back(model.id);
            labels.push_back(Widen(model.Label()));
        }
    } else {
        for (const std::string& effort : current->efforts) {
            values.push_back(effort);
            std::wstring label = Widen(effort);
            if (!label.empty()) label[0] = static_cast<wchar_t>(towupper(label[0]));
            labels.push_back(std::move(label));
        }
    }
    HMENU menu = CreatePopupMenu();
    if (menu == nullptr) {
        return;
    }
    // The agent's own default first, then what it offers; a disabled note when it offers nothing.
    AppendMenuW(menu, MF_STRING | (selected.empty() ? MF_CHECKED : 0U), kFirstChoiceCommand,
        Widen(models ? EditorText("agent.defaultModel", "Default (the agent's own setting)") : EditorText("agent.defaultEffort", "Default (the agent's own setting)")).c_str());
    for (std::size_t index = 0; index < values.size(); ++index) {
        AppendMenuW(menu, MF_STRING | (values[index] == selected ? MF_CHECKED : 0U), kFirstChoiceCommand + 1U + static_cast<UINT>(index), labels[index].c_str());
    }
    if (values.empty()) {
        AppendMenuW(menu, MF_STRING | MF_GRAYED, 0U, Widen(models ? EditorText("agent.noModels", "This agent picks its model itself")
                                                              : EditorText("agent.noEfforts", "This agent has no reasoning levels")).c_str());
    }
    POINT point{ anchor.left, anchor.bottom };
    ClientToScreen(owner, &point);
    const int chosen = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_LEFTALIGN | TPM_TOPALIGN | TPM_NONOTIFY, point.x, point.y, 0, owner, nullptr);
    DestroyMenu(menu);
    if (chosen < static_cast<int>(kFirstChoiceCommand)) {
        return;
    }
    const std::size_t index = static_cast<std::size_t>(chosen) - kFirstChoiceCommand;
    const std::string value = index == 0U ? std::string{} : values[index - 1U];
    if (models) {
        agent.SelectModel(value);
    } else {
        agent.SelectEffort(value);
    }
}

void EditorAgentPanelController::ShowModelMenu(HWND owner, POINT screenAnchor) {
    EditorAgentPanelState& agent = sceneContext_.AgentPanel();
    agent.Prepare();
    HMENU menu = CreatePopupMenu();
    if (menu == nullptr) {
        return;
    }
    // Each agent (provider and profile) as a heading over its models; an agent that picks its
    // model itself is one item.
    const std::vector<EditorAgent>& agents = agent.Agents();
    for (std::size_t index = 0; index < agents.size(); ++index) {
        const EditorAgent& entry = agents[index];
        const bool current = static_cast<int>(index) == agent.CurrentIndex();
        const UINT base = kFirstModelCommand + static_cast<UINT>(index) * kModelsPerAgent;
        if (index > 0U) {
            AppendMenuW(menu, MF_SEPARATOR, 0U, nullptr);
        }
        if (entry.models.empty()) {
            AppendMenuW(menu, MF_STRING | (current ? MF_CHECKED : 0U), base, Widen(entry.Label()).c_str());
            continue;
        }
        AppendMenuW(menu, MF_STRING | MF_GRAYED, 0U, Widen(entry.Label()).c_str());
        AppendMenuW(menu, MF_STRING | (current && agent.Model().empty() ? MF_CHECKED : 0U), base,
            Widen("    " + std::string{ EditorText("agent.defaultModelShort", "Default model") }).c_str());
        for (std::size_t model = 0; model < entry.models.size() && model + 1U < kModelsPerAgent; ++model) {
            AppendMenuW(menu, MF_STRING | (current && agent.Model() == entry.models[model].id ? MF_CHECKED : 0U),
                base + 1U + static_cast<UINT>(model), Widen("    " + entry.models[model].Label()).c_str());
        }
    }
    // The reasoning level of the current agent, in plain words.
    if (const EditorAgent* current = agent.Current(); current != nullptr && !current->efforts.empty()) {
        HMENU efforts = CreatePopupMenu();
        AppendMenuW(efforts, MF_STRING | (agent.Effort().empty() ? MF_CHECKED : 0U), kFirstEffortCommand,
            Widen(EditorText("agent.defaultEffort", "Default (the agent's own setting)")).c_str());
        for (std::size_t index = 0; index < current->efforts.size(); ++index) {
            AppendMenuW(efforts, MF_STRING | (agent.Effort() == current->efforts[index] ? MF_CHECKED : 0U), kFirstEffortCommand + 1U + static_cast<UINT>(index),
                Widen(AgentBarRenderer::EffortLabel(current->efforts[index])).c_str());
        }
        if (!agents.empty()) {
            AppendMenuW(menu, MF_SEPARATOR, 0U, nullptr);
        }
        const std::string title = std::string{ EditorText("agent.reasoningMenu", "Reasoning") } +
            (agent.Effort().empty() ? std::string{} : ": " + AgentBarRenderer::EffortLabel(agent.Effort()));
        AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(efforts), Widen(title).c_str());
    }
    AppendMenuW(menu, MF_SEPARATOR, 0U, nullptr);
    AppendMenuW(menu, MF_STRING, kRefreshCommand, Widen(EditorText("agent.refresh", "Refresh (find installed agents again)")).c_str());
    AppendMenuW(menu, MF_STRING, kEditAgentsCommand, Widen(EditorText("agent.editAgents", "Add your own agent...")).c_str());
    const int chosen = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_LEFTALIGN | TPM_BOTTOMALIGN | TPM_NONOTIFY, screenAnchor.x, screenAnchor.y, 0, owner, nullptr);
    DestroyMenu(menu);
    if (chosen <= 0) {
        return;
    }
    const UINT command = static_cast<UINT>(chosen);
    if (command >= kFirstModelCommand) {
        const UINT index = (command - kFirstModelCommand) / kModelsPerAgent;
        const UINT model = (command - kFirstModelCommand) % kModelsPerAgent;
        if (index >= agents.size()) {
            return;
        }
        agent.SelectAgent(static_cast<int>(index));
        agent.SelectModel(model == 0U ? std::string{} : agents[index].models[model - 1U].id);
    } else if (command >= kFirstEffortCommand) {
        const EditorAgent* current = agent.Current();
        const UINT index = command - kFirstEffortCommand;
        if (current != nullptr && index <= current->efforts.size()) {
            agent.SelectEffort(index == 0U ? std::string{} : current->efforts[index - 1U]);
        }
    } else if (command == kRefreshCommand) {
        agent.RefreshAgents();
    } else if (command == kEditAgentsCommand && EnsureEditorAgentsFile()) {
        ShellExecuteW(owner, L"open", L"notepad.exe", EditorAgentsFile().wstring().c_str(), nullptr, SW_SHOWNORMAL);
    }
}

std::optional<EditorAgentWorkspace> EditorAgentPanelController::PrepareRun() {
    EditorAgentPanelState& agent = sceneContext_.AgentPanel();
    // The agent works on the files: every unsaved edit (the scene and any open asset editor) goes to
    // disk first, so it sees what you see and an editor never writes an older copy over its work.
    if (!sceneContext_.CurrentScenePath().empty() && !sceneContext_.SaveOpenDocuments()) {
        agent.Add(EditorAgentEntryKind::Error, std::string{ EditorText("agent.unsavedNotSent",
            "Your unsaved changes could not be saved, so the request was not sent. Save them, then send again.") });
        return std::nullopt;
    }
    // The agent's map of the project (AGENTS.md and the script API) is what kb_cli init-agent
    // writes; a project that never had it gets it before its first request, so the agent does not
    // start by searching the folders.
    EditorAgentWorkspace workspace = Workspace();
    {
        std::vector<std::string> selected;
        for (const kb::scene::SceneEntity entity : sceneContext_.SelectedHierarchyEntities()) {
            selected.push_back(sceneContext_.Scene().Entities().Name(entity));
        }
        std::error_code error;
        const std::filesystem::path scene = sceneContext_.CurrentScenePath().empty()
            ? std::filesystem::path{} : sceneContext_.CurrentScenePath().lexically_relative(workspace.project);
        workspace.context = kb::project::AgentEditorContextLine(scene.generic_string(), selected);
    }
    // Written once per editor session, so a project made by an older engine gets the current guide
    // too (a guide a person edited is kept as it is).
    static std::filesystem::path guided;
    if (!workspace.project.empty() && guided != workspace.project) {
        guided = workspace.project;
        kb::script::ScriptRuntimeHost host{ sceneContext_.Scene() };
        const kb::script::ScriptAgentProjectFilesResult written = host.Succeeded()
            ? kb::script::ScriptAgentProjectFiles::Write(workspace.project, kb::script::ScriptApiCatalog::Build(host))
            : kb::script::ScriptAgentProjectFilesResult{ .succeeded = false, .error = "the script runtime could not start" };
        if (!written.succeeded) {
            agent.Add(EditorAgentEntryKind::Error, EditorLocalization::Format("agent.guideNotWritten",
                "The agent's guide to this project (AGENTS.md) could not be written: {error}", { { "error", written.error } }));
        }
    }
    return workspace;
}

bool EditorAgentPanelController::SendOrStop() {
    EditorAgentPanelState& agent = sceneContext_.AgentPanel();
    agent.Prepare();
    if (agent.Running()) {
        agent.Stop();
        return true;
    }
    if (agent.Input().empty() && agent.Attachments().empty()) {
        return false;
    }
    const std::optional<EditorAgentWorkspace> workspace = PrepareRun();
    return workspace.has_value() && agent.Send(*workspace);
}

bool EditorAgentPanelController::PressCardButton(int index) {
    EditorAgentPanelState& agent = sceneContext_.AgentPanel();
    agent.Prepare();
    const std::optional<EditorAgentCard> card = agent.Card();
    if (!card.has_value() || index < 0 || index >= static_cast<int>(card->buttons.size())) return false;
    // Stopping, cancelling and the answers to a suggestion start nothing (but a plan), so they need nothing saved first.
    const EditorAgentCardAction action = card->buttons[static_cast<std::size_t>(index)].action;
    const std::optional<EditorAgentHint> hint = action == EditorAgentCardAction::AcceptAdvice ? agent.Hint() : std::nullopt;
    if (action == EditorAgentCardAction::Stop || action == EditorAgentCardAction::CancelPlan || action == EditorAgentCardAction::DismissAdvice ||
        action == EditorAgentCardAction::ChooseAgent || action == EditorAgentCardAction::ChooseModel ||
        (hint.has_value() && hint->action != EditorAgentHintAction::PlanInSteps)) {
        return agent.PressCard(index, Workspace());
    }
    const std::optional<EditorAgentWorkspace> workspace = PrepareRun();
    return workspace.has_value() && agent.PressCard(index, *workspace);
}

bool EditorAgentPanelController::PressNotice(int index) {
    EditorAgentPanelState& agent = sceneContext_.AgentPanel();
    const std::optional<EditorAgentCard> card = agent.Card();
    if (!card.has_value()) return false;
    if (index < static_cast<int>(card->buttons.size())) return PressCardButton(index);
    // The checklist of a plan is in the docked panel.
    agent.RequestDock(EditorAgentPanelState::DockRequest::Dock);
    return true;
}

bool EditorAgentPanelController::HandleChar(WPARAM character) {
    EditorAgentPanelState& agent = sceneContext_.AgentPanel();
    if (!agent.InputFocused()) {
        return false;
    }
    // Control characters (Enter, Backspace, Esc) arrive here too; WM_KEYDOWN acts on those.
    agent.TypeCharacter(static_cast<wchar_t>(character));
    return true;
}

void EditorAgentPanelController::Paste(HWND owner) {
    EditorAgentPanelState& agent = sceneContext_.AgentPanel();
    if (std::vector<std::filesystem::path> files = ClipboardPictureFiles(owner); !files.empty()) {
        for (std::filesystem::path& file : files) agent.Attach(std::move(file));
        return;
    }
    if (std::optional<std::filesystem::path> picture = SaveClipboardPicture(owner); picture.has_value()) {
        agent.Attach(std::move(*picture));
        return;
    }
    // Text keeps its line breaks: a pasted list or log stays readable for the agent.
    if (const std::optional<std::string> text = kb::platform::ReadWin32ClipboardText(owner); text.has_value()) {
        agent.InsertText(*text);
    }
}

bool EditorAgentPanelController::HandleKeyDown(HWND owner, WPARAM key) {
    EditorAgentPanelState& agent = sceneContext_.AgentPanel();
    if (!agent.InputFocused()) {
        return false;
    }
    const bool control = ControlDown();
    const bool shift = ShiftDown();
    // The selection, or the whole request when nothing is selected.
    const auto copied = [&agent] { return agent.SelectionStart() != agent.SelectionEnd() ? agent.SelectedText() : agent.Input(); };
    switch (EditorTextInputShortcuts::Resolve(key)) {
    case EditorTextInputShortcut::Paste:
        Paste(owner);
        return true;
    case EditorTextInputShortcut::Copy:
        static_cast<void>(EditorTextInputShortcuts::CopyToClipboard(owner, copied()));
        return true;
    case EditorTextInputShortcut::Cut:
        static_cast<void>(EditorTextInputShortcuts::CopyToClipboard(owner, copied()));
        if (agent.SelectionStart() == agent.SelectionEnd()) agent.SelectAll();
        agent.Delete();
        return true;
    case EditorTextInputShortcut::SelectAll:
        agent.SelectAll();
        return true;
    case EditorTextInputShortcut::None:
        break;
    }
    if (control && (key == 'Z' || key == 'Y')) {
        if (key == 'Y' || shift) agent.Redo();
        else agent.Undo();
        return true;
    }
    using Move = EditorAgentPanelState::CaretMove;
    switch (key) {
    case VK_TAB:
        // With a bubble waiting for a decision, Tab goes from button to button (Shift+Tab back) and Enter presses the one it is on.
        if (agent.Card().has_value()) agent.MoveCardFocus(shift ? -1 : 1);
        return true;
    case VK_RETURN:
        if (agent.CardFocus() >= 0 && agent.Input().empty()) {
            static_cast<void>(PressCardButton(agent.CardFocus()));
            return true;
        }
        // Shift+Enter starts a new line; Enter sends.
        if (shift) {
            agent.InsertText("\n");
        } else {
            static_cast<void>(SendOrStop());
        }
        return true;
    case VK_BACK:
        agent.Backspace(control);
        return true;
    case VK_DELETE:
        agent.Delete(control);
        return true;
    case VK_LEFT:
        agent.MoveCaret(control ? Move::WordLeft : Move::Left, shift);
        return true;
    case VK_RIGHT:
        agent.MoveCaret(control ? Move::WordRight : Move::Right, shift);
        return true;
    case VK_HOME:
        agent.MoveCaret(control ? Move::Start : Move::LineStart, shift);
        return true;
    case VK_END:
        agent.MoveCaret(control ? Move::End : Move::LineEnd, shift);
        return true;
    case VK_UP:
        agent.MoveCaret(Move::Up, shift);
        return true;
    case VK_DOWN:
        agent.MoveCaret(Move::Down, shift);
        return true;
    case VK_ESCAPE:
        // Esc closes the bubble's picker first, then lets go of the keyboard.
        if (agent.ModelMenuOpen()) {
            agent.SetModelMenuOpen(false);
        } else if (agent.CardFocus() >= 0) {
            agent.ClearCardFocus();
        } else {
            agent.FocusInput(false);
        }
        return true;
    default:
        // Plain keys belong to the request box; Ctrl shortcuts (Ctrl+S) still reach the editor.
        return !ControlDown();
    }
}

} // namespace kb::editor

#endif
