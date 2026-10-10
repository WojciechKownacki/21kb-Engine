#pragma once

#include "engine/platform/FileSystemPath.hpp"
#include "engine/project/ProjectDescriptorReader.hpp"

#include <cstddef>
#include <filesystem>
#include <string>
#include <string_view>

namespace kb::project {

// Windows resolves an ordinary file path against MAX_PATH, and the files the engine writes inside a
// project - the deepest is the skeletal mesh cache entry, 63 characters - go through it. A project
// directory that leaves too little room for them does not fail at the door: it fails later, in
// whichever file operation happens to cross the limit first, with the operating system's own
// message and nothing in it to act on. The reserve is that 63 plus room for the asset tree the user
// builds themselves.
inline constexpr std::size_t kReservedProjectRelativePathLength = 120U;
inline constexpr std::size_t kMaxProjectDirectoryLength =
    kb::platform::kWindowsLegacyMaxPathLength - kReservedProjectRelativePathLength;

class ProjectManager {
public:
    ProjectManager() = delete;

    [[nodiscard]] static std::string_view Extension() noexcept;
    // Empty when the project directory leaves room for the files that go inside it; otherwise the
    // sentence to show, carrying the three numbers the reader needs to act on. Always empty off
    // Windows, where the limit is 4096 and a project cannot realistically reach it.
    [[nodiscard]] static std::string PathBudgetError(const std::filesystem::path& path);
    [[nodiscard]] static bool IsProjectFile(const std::filesystem::path& path);
    // The one rule for "which descriptor is this project": a .21kbproject file is itself; a
    // directory must hold exactly one *.21kbproject (any name - the Hub and `kb_cli new` write
    // <Name>.21kbproject). Empty path plus `error` when there is none or more than one.
    [[nodiscard]] static std::filesystem::path FindProjectFile(const std::filesystem::path& fileOrDirectory, std::string& error);
    [[nodiscard]] static bool IsProjectDirectory(const std::filesystem::path& directory);
    [[nodiscard]] static bool SaveProject(const std::filesystem::path& path, const ProjectDescriptor& descriptor);
    [[nodiscard]] static ProjectDescriptorReadResult LoadProject(const std::filesystem::path& path);
    [[nodiscard]] static bool CreateProject(const std::filesystem::path& path, const ProjectDescriptor& descriptor);
};

} // namespace kb::project
