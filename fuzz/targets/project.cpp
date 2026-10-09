// Project descriptors and the INI settings files a project and the editor read.
#include "FuzzSupport.hpp"

#include "engine/config/IniDocument.hpp"
#include "engine/project/ProjectDescriptorReader.hpp"
#include "engine/project/ProjectSettings.hpp"

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    if (size == 0U) {
        return 0;
    }
    if ((data[0] & 1U) == 0U) {
        static_cast<void>(kb::project::ProjectDescriptorReader::Read(
            kb::fuzz::WriteScratchFile(data + 1, size - 1U, "Project.21kbproject")));
    } else {
        const std::filesystem::path path = kb::fuzz::WriteScratchFile(data + 1, size - 1U, "ProjectSettings.ini");
        static_cast<void>(kb::project::ProjectSettingsStore::Load(path));
        kb::config::IniDocument document;
        std::string error;
        static_cast<void>(document.Load(path, error));
    }
    return 0;
}
