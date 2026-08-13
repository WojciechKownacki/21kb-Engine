#include "engine/project/ParticleProjectPolicy.hpp"

#include <algorithm>
#include <system_error>

namespace kb::project {
namespace {

[[nodiscard]] bool IsWithin(const std::filesystem::path& root, const std::filesystem::path& candidate) {
    auto rootIterator = root.begin();
    auto candidateIterator = candidate.begin();
    for (; rootIterator != root.end(); ++rootIterator, ++candidateIterator) {
        if (candidateIterator == candidate.end() || *rootIterator != *candidateIterator) return false;
    }
    return true;
}

} // namespace

ParticleProjectPolicyResult ParticleProjectPolicy::Inspect(
    const std::filesystem::path& projectRoot,
    const ProjectDescriptor& descriptor) {
    std::error_code error;
    const std::filesystem::path normalizedProjectRoot = std::filesystem::weakly_canonical(projectRoot, error);
    if (error) {
        return { .requirement = ParticleProjectRequirement::ScanFailed, .diagnostic = "project root could not be resolved for particle effect asset scan" };
    }
    const std::filesystem::path contentRoot = std::filesystem::weakly_canonical(normalizedProjectRoot / descriptor.contentRoot, error);
    if (error) {
        return { .requirement = ParticleProjectRequirement::ScanFailed, .diagnostic = "particle effect content root could not be resolved" };
    }
    if (!IsWithin(normalizedProjectRoot, contentRoot)) {
        return { .requirement = ParticleProjectRequirement::InvalidContentRoot,
            .diagnostic = "project content root escapes the project directory; particle effect asset scan was refused" };
    }

    std::filesystem::recursive_directory_iterator iterator{
        contentRoot,
        std::filesystem::directory_options::none,
        error,
    };
    if (error) {
        std::error_code existsError;
        if (!std::filesystem::exists(contentRoot, existsError) && !existsError) return {};
        return { .requirement = ParticleProjectRequirement::ScanFailed, .diagnostic = "particle effect asset scan failed" };
    }

    std::size_t visited = 0U;
    std::size_t effects = 0U;
    const std::filesystem::recursive_directory_iterator end;
    for (; iterator != end; iterator.increment(error)) {
        if (error) return { .requirement = ParticleProjectRequirement::ScanFailed, .diagnostic = "particle effect asset scan failed" };
        if (++visited > MaxScannedEntries) {
            return { .requirement = ParticleProjectRequirement::ScanLimitExceeded, .diagnostic = "particle effect asset scan exceeded its entry limit" };
        }
        if (iterator->is_regular_file(error) && !error && iterator->path().extension() == ".kbvfx") ++effects;
        error.clear();
    }
    if (effects == 0U) return {};

    const auto plugin = std::find_if(descriptor.plugins.begin(), descriptor.plugins.end(), [](const ProjectPluginReference& value) {
        return value.name == PluginId;
    });
    if (plugin == descriptor.plugins.end()) {
        return { .requirement = ParticleProjectRequirement::Missing, .effectAssetCount = effects,
            .diagnostic = "project contains .kbvfx assets; add Rendering.21kbParticle to the project" };
    }
    if (!plugin->enabled) {
        return { .requirement = ParticleProjectRequirement::Disabled, .effectAssetCount = effects,
            .diagnostic = "project contains .kbvfx assets; enable Rendering.21kbParticle in the project" };
    }
    return { .requirement = ParticleProjectRequirement::Enabled, .effectAssetCount = effects };
}

bool ParticleProjectPolicy::Enable(ProjectDescriptor& descriptor, std::string binaryPath) {
    const auto plugin = std::find_if(descriptor.plugins.begin(), descriptor.plugins.end(), [](const ProjectPluginReference& value) {
        return value.name == PluginId;
    });
    if (plugin != descriptor.plugins.end()) {
        const bool changed = !plugin->enabled || plugin->binaryPath != binaryPath;
        plugin->enabled = true;
        plugin->binaryPath = std::move(binaryPath);
        return changed;
    }
    descriptor.plugins.push_back({ .name = std::string{ PluginId }, .binaryPath = std::move(binaryPath), .enabled = true });
    return true;
}

} // namespace kb::project
