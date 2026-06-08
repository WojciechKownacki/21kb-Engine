#pragma once

#include <cstdint>
#include <filesystem>
#include <string>

namespace kb::project {

struct ProjectDescriptorMeta {
    static constexpr std::uint32_t CurrentFileVersion = 1U;

    std::uint32_t fileVersion = CurrentFileVersion;
    std::string projectName;
    std::string engineAssociation;
    std::string defaultScene;
    std::filesystem::path projectFile;
    std::uint64_t byteSize = 0U;
    std::uint64_t contentHashFnv1a64 = 0U;
    std::uint32_t contentChecksumCrc32 = 0U;
    std::uint32_t targetPlatformCount = 0U;
    std::uint32_t moduleCount = 0U;
    std::uint32_t pluginCount = 0U;
};

} // namespace kb::project
