#include "engine/project/ProjectManager.hpp"

#include "engine/project/ProjectDescriptorWriter.hpp"
#include "project/ProjectDescriptorFormat.hpp"

#include <string>
#include <system_error>

namespace kb::project {

std::string_view ProjectManager::Extension() noexcept {
    return ProjectDescriptorFormat::Extension;
}

bool ProjectManager::IsProjectFile(const std::filesystem::path& path) {
    return path.extension() == ProjectDescriptorFormat::Extension;
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
