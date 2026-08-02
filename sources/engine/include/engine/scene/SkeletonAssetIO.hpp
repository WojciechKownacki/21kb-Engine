#pragma once

#include "engine/scene/SkeletonAsset.hpp"

#include <filesystem>
#include <optional>

namespace kb::scene {

inline constexpr const char* kSkeletonAssetExtension = ".kbskeleton";
inline constexpr const char* kSkeletonAssetType = "Skeleton";

class SkeletonAssetIO final {
public:
    SkeletonAssetIO() = delete;

    [[nodiscard]] static std::optional<SkeletonAsset> Load(const std::filesystem::path& path);
    [[nodiscard]] static bool Save(const std::filesystem::path& path, const SkeletonAsset& asset);
};

} // namespace kb::scene
