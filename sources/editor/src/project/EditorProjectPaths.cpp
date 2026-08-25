#include "project/EditorProjectPaths.hpp"

#include <cctype>
#include <system_error>

namespace kb::editor {
namespace {

std::filesystem::path g_projectFile;

[[nodiscard]] bool Exists(const std::filesystem::path& path) noexcept {
    std::error_code error;
    return std::filesystem::exists(path, error) && !error;
}

[[nodiscard]] bool IsDevRepositoryRoot(const std::filesystem::path& root) noexcept {
    return Exists(root / "sources" / "editor" / "src" / "project" / "EditorProjectPaths.cpp") &&
        Exists(root / "Project" / "Project.21kbproject");
}

[[nodiscard]] std::filesystem::path ParentPath(std::filesystem::path path) {
    const std::filesystem::path parent = path.parent_path();
    return parent == path ? std::filesystem::path{} : parent;
}

[[nodiscard]] std::filesystem::path ResolveDefaultProjectRoot() {
    std::error_code error;
    std::filesystem::path current = std::filesystem::absolute(std::filesystem::current_path(error), error);
    if (error || current.empty()) {
        current = std::filesystem::current_path();
    }

    for (std::filesystem::path probe = current; !probe.empty(); probe = ParentPath(probe)) {
        if (IsDevRepositoryRoot(probe)) {
            return probe / "Project";
        }
    }

    for (std::filesystem::path probe = current; !probe.empty(); probe = ParentPath(probe)) {
        const std::filesystem::path candidate = probe / "Project";
        if (Exists(candidate / "Project.21kbproject")) {
            return candidate;
        }
    }

    return current / "Project";
}

[[nodiscard]] std::string Sanitize(std::string name) {
    for (char& character : name) {
        const unsigned char value = static_cast<unsigned char>(character);
        if (!std::isalnum(value) && character != '-' && character != '_') {
            character = '_';
        }
    }
    return name.empty() ? std::string{ "Prefab" } : name;
}

} // namespace

void EditorProjectPaths::SetProjectFile(std::filesystem::path projectFile) {
    if (projectFile.empty()) {
        g_projectFile.clear();
        return;
    }

    std::error_code error;
    g_projectFile = std::filesystem::absolute(std::move(projectFile), error);
    if (error) {
        g_projectFile.clear();
    }
}

std::filesystem::path EditorProjectPaths::AssetsRoot() {
    return ProjectRoot() / "Assets";
}

std::filesystem::path EditorProjectPaths::ProjectFile() {
    if (!g_projectFile.empty()) {
        return g_projectFile;
    }
    return ProjectRoot() / "Project.21kbproject";
}

std::filesystem::path EditorProjectPaths::ProjectRoot() {
    if (!g_projectFile.empty()) {
        return g_projectFile.parent_path();
    }
    return ResolveDefaultProjectRoot();
}

std::filesystem::path EditorProjectPaths::ScenesRoot() {
    return AssetsRoot() / "Scenes";
}

std::filesystem::path EditorProjectPaths::PrefabsRoot() {
    return AssetsRoot() / "Prefabs";
}

std::filesystem::path EditorProjectPaths::DefaultScenePath() {
    return ScenesRoot() / "Main.21kbscene";
}

std::filesystem::path EditorProjectPaths::EditorSettingsFile() {
    return ProjectRoot() / ".21kb" / "EditorSettings.txt";
}

std::filesystem::path EditorProjectPaths::UniquePrefabPath(std::string name) {
    return UniquePrefabPathInFolder(PrefabsRoot(), std::move(name));
}

std::filesystem::path EditorProjectPaths::UniquePrefabPathInFolder(const std::filesystem::path& folder, std::string name) {
    std::filesystem::path root = folder;
    std::filesystem::create_directories(root);

    const std::string base = Sanitize(std::move(name));
    std::filesystem::path candidate = root / (base + ".kbprefab");
    int suffix = 1;
    while (std::filesystem::exists(candidate)) {
        candidate = root / (base + "_" + std::to_string(suffix++) + ".kbprefab");
    }
    return candidate;
}

std::filesystem::path EditorProjectPaths::UniqueScenePath(std::string name) {
    std::filesystem::create_directories(ScenesRoot());

    const std::string base = Sanitize(std::move(name));
    std::filesystem::path candidate = ScenesRoot() / (base + ".21kbscene");
    int suffix = 1;
    while (std::filesystem::exists(candidate)) {
        candidate = ScenesRoot() / (base + "_" + std::to_string(suffix++) + ".21kbscene");
    }
    return candidate;
}

} // namespace kb::editor
