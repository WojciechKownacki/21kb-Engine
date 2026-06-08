#pragma once

#include <cstdint>
#include <filesystem>

namespace kb::project {

struct ProjectDescriptorIntegrity {
    std::uint64_t byteSize = 0U;
    std::uint64_t contentHashFnv1a64 = 0U;
    std::uint32_t contentChecksumCrc32 = 0U;
};

class ProjectDescriptorIntegrityService {
public:
    ProjectDescriptorIntegrityService() = delete;

    [[nodiscard]] static ProjectDescriptorIntegrity ComputeFile(const std::filesystem::path& path) noexcept;
};

} // namespace kb::project
