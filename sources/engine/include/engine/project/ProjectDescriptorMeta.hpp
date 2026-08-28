#pragma once

#include <cstdint>
#include <filesystem>
#include <string>

namespace kb::project {

// The descriptor's integrity record. Version 2 dropped the project name and the
// default scene: both are settings now, and a summary that repeats them would go
// stale the moment someone edits the settings file.
struct ProjectDescriptorMeta {
    static constexpr std::uint32_t CurrentFileVersion = 2U;

    std::uint32_t fileVersion = CurrentFileVersion;
    std::string engineAssociation;
    std::filesystem::path projectFile;
    std::uint64_t byteSize = 0U;
    std::uint64_t contentHashFnv1a64 = 0U;
    std::uint32_t contentChecksumCrc32 = 0U;
    std::uint32_t targetPlatformCount = 0U;
    std::uint32_t moduleCount = 0U;
    std::uint32_t pluginCount = 0U;
};

} // namespace kb::project
