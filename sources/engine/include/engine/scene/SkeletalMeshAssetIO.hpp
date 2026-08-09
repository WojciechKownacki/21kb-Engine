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

struct SkeletalMeshAssetBinding {
    std::uint64_t skeletonAssetId = 0U;
    std::uint64_t skeletonCompatibilitySignature = 0U;
};

class SkeletalMeshAssetIO final {
public:
    SkeletalMeshAssetIO() = delete;
    [[nodiscard]] static std::optional<SkeletalMeshAssetBinding> LoadBinding(
        const std::filesystem::path& path,
        std::string* error = nullptr);
    [[nodiscard]] static std::optional<SkeletalMeshAsset> Load(
        const std::filesystem::path& path,
        std::string* error = nullptr);
    [[nodiscard]] static std::optional<SkeletalMeshAsset> LoadDerivedData(
        const std::filesystem::path& sourcePath,
        std::uint64_t sourceContentHash,
        std::string* error = nullptr);
    // Precondition: asset passed canonical Load/Save validation. Derived data is disposable and a
    // read validates it again before publication.
    [[nodiscard]] static bool SaveDerivedData(
        const std::filesystem::path& sourcePath,
        std::uint64_t sourceContentHash,
        const SkeletalMeshAsset& asset);
    [[nodiscard]] static bool Save(const std::filesystem::path& path, const SkeletalMeshAsset& asset);
};

} // namespace kb::scene
