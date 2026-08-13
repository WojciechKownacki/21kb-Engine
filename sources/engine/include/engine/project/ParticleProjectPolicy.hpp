#pragma once

#include "engine/project/ProjectDescriptor.hpp"

#include <cstddef>
#include <filesystem>
#include <string>
#include <string_view>

namespace kb::project {

enum class ParticleProjectRequirement {
    NotRequired,
    Enabled,
    Missing,
    Disabled,
    InvalidContentRoot,
    ScanFailed,
    ScanLimitExceeded,
};

struct ParticleProjectPolicyResult {
    ParticleProjectRequirement requirement = ParticleProjectRequirement::NotRequired;
    std::size_t effectAssetCount = 0U;
    std::string diagnostic;

    [[nodiscard]] bool IsRunnable() const noexcept {
        return requirement == ParticleProjectRequirement::NotRequired || requirement == ParticleProjectRequirement::Enabled;
    }
};

class ParticleProjectPolicy final {
public:
    static constexpr std::string_view PluginId = "Rendering.21kbParticle";
    static constexpr std::size_t MaxScannedEntries = 100'000U;

    ParticleProjectPolicy() = delete;

    [[nodiscard]] static ParticleProjectPolicyResult Inspect(
        const std::filesystem::path& projectRoot,
        const ProjectDescriptor& descriptor);
    [[nodiscard]] static bool Enable(ProjectDescriptor& descriptor, std::string binaryPath);
};

} // namespace kb::project
