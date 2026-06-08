#include "HubProjectActions.hpp"

#include "HubText.hpp"
#include "engine/project/ProjectManager.hpp"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <commdlg.h>
#include <shlobj.h>
#endif

#include <system_error>

namespace kb::hub {
namespace {

[[nodiscard]] std::filesystem::path ExecutableDirectory() {
    wchar_t buffer[MAX_PATH]{};
    const DWORD length = GetModuleFileNameW(nullptr, buffer, static_cast<DWORD>(std::size(buffer)));
    if (length == 0 || length >= std::size(buffer)) {
        return std::filesystem::current_path();
    }
    return std::filesystem::path{ buffer }.parent_path();
}

[[nodiscard]] std::filesystem::path DocumentsDirectory() {
    wchar_t buffer[MAX_PATH]{};
    const HRESULT result = SHGetFolderPathW(nullptr, CSIDL_PERSONAL, nullptr, SHGFP_TYPE_CURRENT, buffer);
    if (FAILED(result)) {
        return std::filesystem::current_path();
    }
    return std::filesystem::path{ buffer };
}

[[nodiscard]] kb::project::ProjectDescriptor MakeDescriptor(std::wstring_view name) {
    kb::project::ProjectDescriptor descriptor;
    descriptor.name = HubText::WideToUtf8(name);
    descriptor.category = "Game";
    descriptor.description = "21kb project";
    descriptor.contentRoot = "Assets";
    descriptor.defaultScene = "/Game/Scenes/Main.21kbscene";
    descriptor.targetPlatforms = { "Windows" };
    return descriptor;
}

[[nodiscard]] std::wstring Quote(std::wstring value) {
    std::wstring escaped;
    escaped.reserve(value.size() + 2);
    escaped.push_back(L'"');
    for (const wchar_t character : value) {
        if (character == L'"') {
            escaped.push_back(L'\\');
        }
        escaped.push_back(character);
    }
    escaped.push_back(L'"');
    return escaped;
}

} // namespace

std::filesystem::path HubProjectActions::DefaultProjectLocation() {
    return DocumentsDirectory() / "21kb Projects";
}

std::optional<std::filesystem::path> HubProjectActions::BrowseProjectFile(HWND owner) {
    wchar_t fileName[MAX_PATH]{};
    OPENFILENAMEW dialog{};
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = owner;
    dialog.lpstrFilter = L"21kb Project (*.21kbproject)\0*.21kbproject\0All files (*.*)\0*.*\0";
    dialog.lpstrFile = fileName;
    dialog.nMaxFile = static_cast<DWORD>(std::size(fileName));
    dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    dialog.lpstrDefExt = L"21kbproject";

    if (GetOpenFileNameW(&dialog) == FALSE) {
        return std::nullopt;
    }
    return std::filesystem::path{ fileName };
}

std::optional<std::filesystem::path> HubProjectActions::BrowseFolder(HWND owner, const std::filesystem::path& initialFolder) {
    BROWSEINFOW browse{};
    browse.hwndOwner = owner;
    browse.lpszTitle = L"Choose project location";
    browse.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
    PIDLIST_ABSOLUTE pidl = SHBrowseForFolderW(&browse);
    if (pidl == nullptr) {
        return std::nullopt;
    }

    wchar_t path[MAX_PATH]{};
    const BOOL ok = SHGetPathFromIDListW(pidl, path);
    CoTaskMemFree(pidl);
    if (ok == FALSE) {
        return initialFolder.empty() ? std::nullopt : std::optional<std::filesystem::path>{ initialFolder };
    }
    return std::filesystem::path{ path };
}

std::optional<std::filesystem::path> HubProjectActions::BrowseProjectFolder(HWND owner) {
    IFileDialog* dialog = nullptr;
    const HRESULT created = CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog));
    if (FAILED(created) || dialog == nullptr) {
        return BrowseFolder(owner, DefaultProjectLocation());
    }

    DWORD options = 0;
    if (SUCCEEDED(dialog->GetOptions(&options))) {
        static_cast<void>(dialog->SetOptions(options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST));
    }
    static_cast<void>(dialog->SetTitle(L"Choose or create a 21kb project folder"));

    IShellItem* initialFolder = nullptr;
    const std::filesystem::path defaultLocation = DefaultProjectLocation();
    if (SUCCEEDED(SHCreateItemFromParsingName(defaultLocation.wstring().c_str(), nullptr, IID_PPV_ARGS(&initialFolder)))) {
        static_cast<void>(dialog->SetFolder(initialFolder));
        initialFolder->Release();
    }

    const HRESULT shown = dialog->Show(owner);
    if (FAILED(shown)) {
        dialog->Release();
        return std::nullopt;
    }

    IShellItem* result = nullptr;
    if (FAILED(dialog->GetResult(&result)) || result == nullptr) {
        dialog->Release();
        return std::nullopt;
    }

    PWSTR filePath = nullptr;
    const HRESULT nameResult = result->GetDisplayName(SIGDN_FILESYSPATH, &filePath);
    result->Release();
    dialog->Release();
    if (FAILED(nameResult) || filePath == nullptr) {
        return std::nullopt;
    }

    std::filesystem::path path{ filePath };
    CoTaskMemFree(filePath);
    return path;
}

HubCreateProjectResult HubProjectActions::CreateProjectInFolder(const std::filesystem::path& projectRoot) {
    if (projectRoot.empty()) {
        return HubCreateProjectResult{ .succeeded = false, .projectFile = {}, .error = L"Project folder is empty." };
    }

    const std::wstring projectName = HubText::SanitizeProjectName(projectRoot.filename().wstring());
    if (projectName.empty()) {
        return HubCreateProjectResult{ .succeeded = false, .projectFile = {}, .error = L"Project folder name is invalid." };
    }

    const std::filesystem::path projectFile = projectRoot / (projectName + L".21kbproject");
    std::error_code error;
    if (std::filesystem::exists(projectFile, error) && !error) {
        return HubCreateProjectResult{ .succeeded = false, .projectFile = projectFile, .error = L"Project descriptor already exists in this folder." };
    }

    std::filesystem::create_directories(projectRoot, error);
    if (error) {
        return HubCreateProjectResult{ .succeeded = false, .projectFile = projectFile, .error = L"Project folder could not be created." };
    }

    const std::filesystem::directory_iterator firstEntry(projectRoot, error);
    if (error) {
        return HubCreateProjectResult{ .succeeded = false, .projectFile = projectFile, .error = L"Project folder could not be inspected." };
    }
    if (firstEntry != std::filesystem::directory_iterator{}) {
        return HubCreateProjectResult{ .succeeded = false, .projectFile = projectFile, .error = L"Choose an empty folder for a new project." };
    }

    std::filesystem::create_directories(projectRoot / "Assets" / "Scenes", error);
    if (error) {
        return HubCreateProjectResult{ .succeeded = false, .projectFile = projectFile, .error = L"Scene folder could not be created." };
    }
    std::filesystem::create_directories(projectRoot / "Assets" / "Prefabs", error);
    if (error) {
        std::filesystem::remove_all(projectRoot / "Assets", error);
        return HubCreateProjectResult{ .succeeded = false, .projectFile = projectFile, .error = L"Prefab folder could not be created." };
    }

    kb::project::ProjectDescriptor descriptor = MakeDescriptor(projectName);
    if (!kb::project::ProjectManager::CreateProject(projectFile, descriptor)) {
        std::filesystem::remove_all(projectRoot / "Assets", error);
        return HubCreateProjectResult{ .succeeded = false, .projectFile = projectFile, .error = L"Project descriptor could not be written." };
    }

    return HubCreateProjectResult{ .succeeded = true, .projectFile = projectFile, .error = {} };
}

bool HubProjectActions::LaunchEditor(HWND owner, const std::filesystem::path& projectFile, std::wstring& error) {
    const std::filesystem::path editor = ExecutableDirectory() / "kb_editor.exe";
    const std::filesystem::path workingDirectory = ExecutableDirectory();
    const std::wstring editorPath = editor.wstring();
    const std::wstring workingDirectoryPath = workingDirectory.wstring();
    std::error_code fileError;
    if (!std::filesystem::is_regular_file(editor, fileError) || fileError) {
        error = L"kb_editor.exe was not found next to kb21hub.exe.";
        MessageBoxW(owner, error.c_str(), L"21kb Hub", MB_ICONERROR | MB_OK);
        return false;
    }

    std::wstring commandLine = Quote(editor.wstring()) + L" --project " + Quote(projectFile.wstring());
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    const BOOL created = CreateProcessW(
        editorPath.c_str(),
        commandLine.data(),
        nullptr,
        nullptr,
        FALSE,
        0,
        nullptr,
        workingDirectoryPath.c_str(),
        &startup,
        &process);

    if (created == FALSE) {
        error = L"Editor process could not be started.";
        MessageBoxW(owner, error.c_str(), L"21kb Hub", MB_ICONERROR | MB_OK);
        return false;
    }

    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return true;
}

} // namespace kb::hub
