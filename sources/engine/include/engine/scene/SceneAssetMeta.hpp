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

struct SceneAssetIntegrity {
    std::uint64_t byteSize = 0;
    std::uint64_t contentHashFnv1a64 = 0;
    std::uint32_t contentChecksumCrc32 = 0;
};

} // namespace kb::scene
