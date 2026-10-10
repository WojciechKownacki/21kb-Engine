#include "engine/project/ProjectManager.hpp"

#include "engine/project/ProjectDescriptorWriter.hpp"
#include "project/ProjectDescriptorFormat.hpp"

#include <string>
#include <vector>
#include <system_error>

namespace kb::project {

std::string_view ProjectManager::Extension() noexcept {
    return ProjectDescriptorFormat::Extension;
}

bool ProjectManager::IsProjectFile(const std::filesystem::path& path) {
    return path.extension() == ProjectDescriptorFormat::Extension;
}

std::filesystem::path ProjectManager::FindProjectFile(const std::filesystem::path& fileOrDirectory, std::string& error) {
    error.clear();
    std::error_code pathError;
    if (std::filesystem::is_regular_file(fileOrDirectory, pathError) && !pathError) {
        if (IsProjectFile(fileOrDirectory)) {
            return fileOrDirectory;
        }
        error = "not a " + std::string{ Extension() } + " file: " + fileOrDirectory.generic_string();
        return {};
    }
    if (!std::filesystem::is_directory(fileOrDirectory, pathError) || pathError) {
        error = "project path was not found: " + fileOrDirectory.generic_string();
        return {};
    }
    std::vector<std::filesystem::path> found;
    for (std::filesystem::directory_iterator entry{ fileOrDirectory, pathError }, end; !pathError && entry != end;
         entry.increment(pathError)) {
        if (entry->is_regular_file(pathError) && IsProjectFile(entry->path())) {
            found.push_back(entry->path());
        }
    }
    if (pathError) {
        error = "project directory could not be read: " + fileOrDirectory.generic_string();
        return {};
    }
    if (found.size() == 1U) {
        return found.front();
    }
    if (found.empty()) {
        error = "no " + std::string{ Extension() } + " file in " + fileOrDirectory.generic_string();
        return {};
    }
    error = "more than one " + std::string{ Extension() } + " file in " + fileOrDirectory.generic_string() + ":";
    for (const std::filesystem::path& file : found) {
        error += " " + file.filename().generic_string();
    }
    return {};
}

bool ProjectManager::IsProjectDirectory(const std::filesystem::path& directory) {
    std::string error;
    return !FindProjectFile(directory, error).empty();
}

std::string ProjectManager::PathBudgetError(const std::filesystem::path& path) {
#if defined(_WIN32)
    std::error_code error;
    std::filesystem::path absolutePath = std::filesystem::absolute(path, error);
    if (error) {
        absolutePath = path;
    }
    const std::filesystem::path directory = kb::platform::PortablePath(absolutePath).parent_path();
    const std::size_t length = directory.native().size();
    if (length <= kMaxProjectDirectoryLength) {
        return {};
    }
    return "Project directory is " + std::to_string(length) +
        " characters. Windows resolves a file path against " +
        std::to_string(kb::platform::kWindowsLegacyMaxPathLength) +
        ", and the engine reserves " + std::to_string(kReservedProjectRelativePathLength) +
        " for the files inside a project, so the directory must be at most " +
        std::to_string(kMaxProjectDirectoryLength) +
        " characters. Move the project to a shorter path: " + directory.generic_string();
#else
    static_cast<void>(path);
    return {};
#endif
}

bool ProjectManager::SaveProject(const std::filesystem::path& path, const ProjectDescriptor& descriptor) {
    return IsProjectFile(path) && PathBudgetError(path).empty() &&
        ProjectDescriptorWriter::Write(path, descriptor);
}

ProjectDescriptorReadResult ProjectManager::LoadProject(const std::filesystem::path& path) {
    if (!IsProjectFile(path)) {
        return ProjectDescriptorReadResult{ .succeeded = false, .descriptor = {}, .error = "Project file must use .21kbproject extension." };
    }
    // Refused here rather than in whichever asset write crosses MAX_PATH first, where the message
    // would be the operating system's and would name neither the project nor what to do about it.
    if (std::string budget = PathBudgetError(path); !budget.empty()) {
        return ProjectDescriptorReadResult{ .succeeded = false, .descriptor = {}, .error = std::move(budget) };
    }
    return ProjectDescriptorReader::Read(path);
}

bool ProjectManager::CreateProject(const std::filesystem::path& path, const ProjectDescriptor& descriptor) {
    if (!IsProjectFile(path)) {
        return false;
    }

    std::error_code error;
    if (std::filesystem::exists(path, error) && !error) {
        return false;
    }
    return SaveProject(path, descriptor);
}

} // namespace kb::project
