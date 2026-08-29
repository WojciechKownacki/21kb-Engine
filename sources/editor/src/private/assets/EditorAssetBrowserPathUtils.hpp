#pragma once

#include "engine/assets/AssetManager.hpp"

#include <filesystem>
#include <set>
#include <string>
#include <unordered_set>

namespace kb::editor::asset_browser {

[[nodiscard]] std::string Normalize(const std::filesystem::path& path);
[[nodiscard]] std::filesystem::path ParentVirtualPath(const std::filesystem::path& virtualPath);
[[nodiscard]] std::string FileNameOrRoot(const std::filesystem::path& path);
[[nodiscard]] int FolderDepth(const std::filesystem::path& path);
[[nodiscard]] std::string Lower(std::string text);
[[nodiscard]] bool StartsWithFolder(const std::filesystem::path& path, const std::filesystem::path& folder);
// Whether a virtual path lives in the project's own content. Other mounts - engine
// and plugin content deployed beside the executable - are browsable, but they are
// not the user's to rewrite, and a write there lands in a build output that the next
// build replaces.
[[nodiscard]] bool IsProjectContent(const std::filesystem::path& virtualPath);
[[nodiscard]] std::set<std::string> AllFolderPaths(const kb::assets::AssetManager& manager);
[[nodiscard]] bool HasImmediateChild(const std::set<std::string>& folders, const std::string& folder);
[[nodiscard]] bool IsHiddenByCollapsedAncestor(const std::string& folder, const std::unordered_set<std::string>& collapsedFolders);
[[nodiscard]] bool FolderExists(const std::filesystem::path& virtualPath, const kb::assets::AssetManager& manager);

} // namespace kb::editor::asset_browser
