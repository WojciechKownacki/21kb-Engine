#include "app/EditorProjectCommands.hpp"

#if defined(_WIN32)
#include "engine/project/ProjectCreator.hpp"
#include "engine/project/ProjectManager.hpp"
#include "localization/EditorLocalization.hpp"
#include "platform/win32/EditorChoiceDialog.hpp"
#include "platform/win32/EditorFileDialogFilter.hpp"
#include "platform/win32/EditorProjectTemplateDialog.hpp"
#include "project/EditorProjectPaths.hpp"
#include "rendering/script_editor/ScriptEditorTextEncoding.hpp"

#include <CommDlg.h>
#include <shellapi.h>

#include <algorithm>
#include <array>
#include <fstream>
#include <optional>
#include <string>
#include <system_error>

namespace kb::editor {
namespace {

constexpr std::size_t kMaximumStored = 24U;
constexpr std::size_t kMaximumShown = 8U;

std::filesystem::path g_pendingProject;

[[nodiscard]] std::string Utf8(const std::filesystem::path& path) {
    const std::u8string text = path.u8string();
    return std::string{ reinterpret_cast<const char*>(text.data()), text.size() };
}

[[nodiscard]] std::filesystem::path FromUtf8(const std::string& text) {
    return std::filesystem::path{ std::u8string{ reinterpret_cast<const char8_t*>(text.data()), text.size() } };
}

[[nodiscard]] std::filesystem::path AppDataRoot() {
    std::array<wchar_t, MAX_PATH> buffer{};
    const DWORD length = GetEnvironmentVariableW(L"APPDATA", buffer.data(), static_cast<DWORD>(buffer.size()));
    return length == 0U || length >= buffer.size() ? std::filesystem::path{} : std::filesystem::path{ buffer.data() } / "21kb";
}

[[nodiscard]] std::filesystem::path EditorStoreFile() {
    return AppDataRoot() / "Editor" / "recent-projects.txt";
}

[[nodiscard]] bool SamePath(const std::filesystem::path& left, const std::filesystem::path& right) {
    std::error_code error;
    return std::filesystem::weakly_canonical(left, error) == std::filesystem::weakly_canonical(right, error);
}

void AddUnique(std::vector<std::filesystem::path>& projects, std::filesystem::path project) {
    if (project.empty()) {
        return;
    }
    if (std::ranges::none_of(projects, [&project](const std::filesystem::path& known) { return SamePath(known, project); })) {
        projects.push_back(std::move(project));
    }
}

[[nodiscard]] std::vector<std::filesystem::path> LoadEditorStore() {
    std::vector<std::filesystem::path> projects;
    std::ifstream input(EditorStoreFile(), std::ios::binary);
    std::string line;
    while (std::getline(input, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        AddUnique(projects, FromUtf8(line));
    }
    return projects;
}

} // namespace

std::vector<EditorPathMenuRow> EditorProjectCommands::RecentProjectRows() {
    std::vector<std::filesystem::path> known = LoadEditorStore();
    // Projects started from the Hub are recent too; the Hub keeps its own list.
    std::wifstream hub(AppDataRoot() / "Hub" / "recent-projects.txt");
    std::wstring line;
    while (std::getline(hub, line)) {
        AddUnique(known, std::filesystem::path{ line });
    }

    std::vector<EditorPathMenuRow> rows;
    const std::filesystem::path current = EditorProjectPaths::ProjectFile();
    for (const std::filesystem::path& project : known) {
        std::error_code error;
        if (rows.size() < kMaximumShown && !SamePath(project, current) && std::filesystem::is_regular_file(project, error)) {
            rows.push_back(EditorPathMenuRow{ .label = Utf8(project.stem()), .detail = Utf8(project.parent_path()), .path = project });
        }
    }
    if (rows.empty()) {
        rows.push_back(EditorPathMenuRow{ .label = std::string{ EditorText("menu.project.noRecent", "No recent projects") } });
    }
    return rows;
}

void EditorProjectCommands::RememberCurrentProject() {
    std::vector<std::filesystem::path> projects;
    AddUnique(projects, EditorProjectPaths::ProjectFile());
    for (std::filesystem::path& project : LoadEditorStore()) {
        AddUnique(projects, std::move(project));
    }
    if (projects.size() > kMaximumStored) {
        projects.resize(kMaximumStored);
    }
    const std::filesystem::path store = EditorStoreFile();
    std::error_code error;
    std::filesystem::create_directories(store.parent_path(), error);
    std::ofstream output(store, std::ios::binary | std::ios::trunc);
    for (const std::filesystem::path& project : projects) {
        output << Utf8(project) << '\n';
    }
}

void EditorProjectCommands::OpenProject(HWND owner, EditorSceneContext& sceneContext) {
    std::array<wchar_t, 32768> fileBuffer{};
    const std::wstring filter = EditorFileDialogFilter({
        { EditorText("fileDialog.projectFilter", "21kb Project (*.21kbproject)"), "*.21kbproject" },
        { EditorText("fileDialog.allFiles", "All files (*.*)"), "*.*" },
    });
    const std::wstring title = ScriptEditorTextEncoding::Widen(EditorText("fileDialog.openProjectTitle", "Open project"));
    OPENFILENAMEW dialog{};
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = owner;
    dialog.lpstrFilter = filter.c_str();
    dialog.lpstrFile = fileBuffer.data();
    dialog.nMaxFile = static_cast<DWORD>(fileBuffer.size());
    dialog.lpstrTitle = title.c_str();
    dialog.lpstrDefExt = L"21kbproject";
    dialog.Flags = OFN_EXPLORER | OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (GetOpenFileNameW(&dialog) != FALSE) {
        SwitchTo(owner, sceneContext, std::filesystem::path{ fileBuffer.data() });
    }
}

void EditorProjectCommands::NewProject(HWND owner, EditorSceneContext& sceneContext) {
    // The template first: it decides the startup scene, the project settings and the files the project starts with.
    const std::optional<kb::scene::SceneTemplate> startupTemplate = EditorProjectTemplateDialog::Show(owner);
    if (!startupTemplate.has_value()) {
        return;
    }
    // One save dialog says both where the project goes and what it is called.
    std::array<wchar_t, 32768> fileBuffer{};
    wcscpy_s(fileBuffer.data(), fileBuffer.size(), L"NewProject");
    const std::wstring filter = EditorFileDialogFilter({
        { EditorText("fileDialog.projectFilter", "21kb Project (*.21kbproject)"), "*.21kbproject" },
    });
    const std::wstring title = ScriptEditorTextEncoding::Widen(EditorText("fileDialog.newProjectTitle", "New project: choose its place and name"));
    const std::wstring initialDirectory = EditorProjectPaths::ProjectRoot().parent_path().wstring();
    OPENFILENAMEW dialog{};
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = owner;
    dialog.lpstrFilter = filter.c_str();
    dialog.lpstrFile = fileBuffer.data();
    dialog.nMaxFile = static_cast<DWORD>(fileBuffer.size());
    dialog.lpstrInitialDir = initialDirectory.c_str();
    dialog.lpstrTitle = title.c_str();
    dialog.lpstrDefExt = L"21kbproject";
    dialog.Flags = OFN_EXPLORER | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (GetSaveFileNameW(&dialog) == FALSE) {
        return;
    }
    const std::filesystem::path chosen{ fileBuffer.data() };
    const std::string name = Utf8(chosen.stem());
    const auto refuse = [&](const std::string& reason) {
        sceneContext.Console().Error("Project", "Project could not be created: " + reason);
        static_cast<void>(EditorChoiceDialog::Show(owner, EditorChoiceDialogDescriptor{
            .title = std::string{ EditorText("project.newTitle", "New Project") },
            .message = reason,
            .icon = HeroIconKind::Warning,
        }));
    };
    if (!kb::project::ProjectCreator::IsValidName(name)) {
        refuse(std::string{ EditorText("project.nameRefused", "A project name may only use letters, digits, dashes and underscores.") });
        return;
    }
    const kb::project::NewProjectResult created = kb::project::ProjectCreator::Create(chosen.parent_path(), name, *startupTemplate);
    if (!created.succeeded) {
        refuse(created.error);
        return;
    }
    SwitchTo(owner, sceneContext, created.projectFile);
}

void EditorProjectCommands::SwitchTo(HWND owner, EditorSceneContext& sceneContext, const std::filesystem::path& projectFile) {
    if (SamePath(projectFile, EditorProjectPaths::ProjectFile())) {
        sceneContext.Console().Info("Project", "This project is already open.");
        return;
    }
    if (!kb::project::ProjectManager::IsProjectFile(projectFile)) {
        sceneContext.Console().Error("Project", "Not a 21kb project file: " + Utf8(projectFile));
        return;
    }
    g_pendingProject = projectFile;
    PostMessageW(owner, WM_CLOSE, 0U, 0);
}

void EditorProjectCommands::ShowInExplorer(HWND owner, EditorSceneContext& sceneContext) {
    const std::wstring root = EditorProjectPaths::ProjectRoot().wstring();
    const auto opened = reinterpret_cast<INT_PTR>(ShellExecuteW(owner, L"open", root.c_str(), nullptr, nullptr, SW_SHOWNORMAL));
    if (opened <= 32) {
        sceneContext.Console().Error("Project", "The project folder could not be opened (error " + std::to_string(static_cast<long long>(opened)) + ").");
    }
}

void EditorProjectCommands::AbandonPendingSwitch() noexcept {
    g_pendingProject.clear();
}

void EditorProjectCommands::LaunchPendingSwitch() {
    if (g_pendingProject.empty()) {
        return;
    }
    std::array<wchar_t, 32768> executable{};
    const DWORD length = GetModuleFileNameW(nullptr, executable.data(), static_cast<DWORD>(executable.size()));
    if (length == 0U || length >= executable.size()) {
        return;
    }
    const std::filesystem::path program{ executable.data() };
    std::wstring line = L"\"" + program.wstring() + L"\" --project \"" + g_pendingProject.wstring() + L"\"";
    const std::wstring directory = program.parent_path().wstring();
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    if (CreateProcessW(program.c_str(), line.data(), nullptr, nullptr, FALSE, 0U, nullptr, directory.c_str(), &startup, &process) != FALSE) {
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
    }
    g_pendingProject.clear();
}

} // namespace kb::editor

#endif
