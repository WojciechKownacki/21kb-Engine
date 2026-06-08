#include "engine/project/ProjectManager.hpp"

#include "engine/project/ProjectDescriptorWriter.hpp"
#include "project/ProjectDescriptorFormat.hpp"

#include <system_error>

namespace kb::project {

std::string_view ProjectManager::Extension() noexcept {
    return ProjectDescriptorFormat::Extension;
}

bool ProjectManager::IsProjectFile(const std::filesystem::path& path) {
    return path.extension() == ProjectDescriptorFormat::Extension;
}

bool ProjectManager::SaveProject(const std::filesystem::path& path, const ProjectDescriptor& descriptor) {
    return IsProjectFile(path) && ProjectDescriptorWriter::Write(path, descriptor);
}

ProjectDescriptorReadResult ProjectManager::LoadProject(const std::filesystem::path& path) {
    if (!IsProjectFile(path)) {
        return ProjectDescriptorReadResult{ .succeeded = false, .descriptor = {}, .error = "Project file must use .21kbproject extension." };
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
