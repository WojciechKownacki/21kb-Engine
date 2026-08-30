#pragma once

#include "engine/assets/AssetId.hpp"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace kb::scene {

struct SceneAssetDependency {
    kb::assets::AssetId assetId{};
    std::string role;
};

struct SceneAssetMeta {
    static constexpr std::uint32_t CurrentFileVersion = 1U;

    std::uint32_t fileVersion = CurrentFileVersion;
    std::string sceneGuid;
    std::string sceneName;
    std::string worldType;
    std::filesystem::path sceneFile;
    std::uint64_t byteSize = 0;
    std::uint64_t contentHashFnv1a64 = 0;
    std::uint32_t contentChecksumCrc32 = 0;
    std::uint32_t rootCount = 0;
    std::uint32_t nodeCount = 0;
    std::vector<SceneAssetDependency> dependencies;
};

// The sidecar path a scene asset's ".meta" descriptor lives at: the scene file
// with its extension replaced. Single source of truth for that mapping, shared by
// SceneAssetWriter (which writes it), SceneAssetReader (which verifies integrity
// against it) and SceneAssetLoader (which reads the dependency list out of it).
[[nodiscard]] inline std::filesystem::path SceneAssetMetaPath(const std::filesystem::path& scenePath) {
    std::filesystem::path metaPath = scenePath;
    metaPath.replace_extension(".meta");
    return metaPath;
}

struct SceneAssetIntegrity {
    std::uint64_t byteSize = 0;
    std::uint64_t contentHashFnv1a64 = 0;
    std::uint32_t contentChecksumCrc32 = 0;
};

} // namespace kb::scene
