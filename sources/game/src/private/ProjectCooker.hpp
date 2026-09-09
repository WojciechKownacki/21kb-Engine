#pragma once

#include <cstddef>
#include <filesystem>
#include <iosfwd>
#include <string>

namespace kb::game {

struct ProjectCookRequest {
    std::filesystem::path projectPath;
    std::string targetProfileId;
    std::filesystem::path outputPackPath;
    std::filesystem::path shadercPath;
    std::filesystem::path engineRoot;
    std::filesystem::path cacheRoot;
    // Windows-only destination for custom project DLL snapshots. The package orchestrator
    // places this directory below the sealed runtime root after a successful cook.
    std::filesystem::path runtimeModulesOutputDirectory;
};

struct ProjectCookResult {
    bool succeeded = false;
    std::size_t assetCount = 0U;
    std::size_t auxiliaryFileCount = 0U;
    std::size_t textureArtifactCount = 0U;
    std::size_t meshArtifactCount = 0U;
    std::size_t shaderArtifactCount = 0U;
    std::string error;
};

[[nodiscard]] ProjectCookResult CookProject(
    const ProjectCookRequest& request,
    std::ostream& diagnostics);

} // namespace kb::game
