#pragma once

#include "engine/scene/SkeletalMeshAsset.hpp"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>

namespace kb::scene {

inline constexpr const char* kSkeletalMeshAssetExtension = ".kbskeletalmesh";
inline constexpr const char* kSkeletalMeshAssetType = "SkeletalMesh";
inline constexpr std::uint32_t kSkeletalMeshAssetSchemaVersion = 1U;

class SkeletalMeshAssetIO final {
public:
    SkeletalMeshAssetIO() = delete;
    [[nodiscard]] static std::optional<SkeletalMeshAsset> Load(
        const std::filesystem::path& path,
        std::string* error = nullptr);
    [[nodiscard]] static bool Save(const std::filesystem::path& path, const SkeletalMeshAsset& asset);
};

} // namespace kb::scene
