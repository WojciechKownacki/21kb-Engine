#include "assets/EditorAssetBrowserPathUtils.hpp"

#include <algorithm>
#include <cctype>

namespace kb::editor::asset_browser {

std::string Normalize(const std::filesystem::path& path) {
    return kb::assets::NormalizeAssetPath(path);
}

std::filesystem::path ParentVirtualPath(const std::filesystem::path& virtualPath) {
    const std::string text = Normalize(virtualPath);
    const std::size_t separator = text.find_last_of('/');
    if (separator == std::string::npos || separator == 0) {
        return std::filesystem::path{ "/Game" };
    }
    return std::filesystem::path{ text.substr(0, separator) };
}

std::string FileNameOrRoot(const std::filesystem::path& path) {
    const std::string text = Normalize(path);
    if (text == "/Game") {
        return "Game";
    }
    return path.filename().string();
}

int FolderDepth(const std::filesystem::path& path) {
    const std::string text = Normalize(path);
    int depth = 0;
    for (const char character : text) {
        if (character == '/') {
            ++depth;
        }
    }
    return std::max(0, depth - 1);
}

std::string Lower(std::string text) {
    std::ranges::transform(text, text.begin(), [](unsigned char value) {
        return static_cast<char>(std::tolower(value));
    });
    return text;
}

bool StartsWithFolder(const std::filesystem::path& path, const std::filesystem::path& folder) {
    const std::string pathText = Normalize(path);
    const std::string folderText = Normalize(folder);
    return pathText == folderText || (pathText.size() > folderText.size() && pathText.starts_with(folderText + "/"));
}

std::set<std::string> AllFolderPaths(const kb::assets::AssetManager& manager) {
    std::set<std::string> folders;
    for (const std::filesystem::path& folder : manager.VirtualFolders()) {
        folders.insert(Normalize(folder));
    }

    for (const kb::assets::AssetMetadata& metadata : manager.Registry().All()) {
        std::filesystem::path folder = ParentVirtualPath(metadata.virtualPath);
        while (!folder.empty()) {
            folders.insert(Normalize(folder));
            const std::filesystem::path parent = ParentVirtualPath(folder);
            if (parent == folder) {
                break;
            }
            folder = parent;
        }
    }
    return folders;
}

bool HasImmediateChild(const std::set<std::string>& folders, const std::string& folder) {
    return std::ranges::any_of(folders, [&folder](const std::string& candidate) {
        return candidate != folder && Normalize(ParentVirtualPath(candidate)) == folder;
    });
}

bool IsHiddenByCollapsedAncestor(const std::string& folder, const std::unordered_set<std::string>& collapsedFolders) {
    std::filesystem::path parent = ParentVirtualPath(folder);
    while (!parent.empty() && Normalize(parent) != folder) {
        const std::string normalizedParent = Normalize(parent);
        if (collapsedFolders.contains(normalizedParent)) {
            return true;
        }
        if (normalizedParent == "/Game") {
            break;
        }
        parent = ParentVirtualPath(parent);
    }
    return false;
}

bool FolderExists(const std::filesystem::path& virtualPath, const kb::assets::AssetManager& manager) {
    const std::string target = Normalize(virtualPath);
    if (target == "/Game") {
        return true;
    }

    return AllFolderPaths(manager).contains(target);
}

} // namespace kb::editor::asset_browser
