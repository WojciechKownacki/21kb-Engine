#pragma once

#include "engine/assets/AssetMetadata.hpp"
#include "engine/assets/AssetMountTable.hpp"

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace kb::assets {

class AssetPathUtilities {
public:
    AssetPathUtilities() = delete;

    [[nodiscard]] static std::string LowerExtension(const std::filesystem::path& extension);
    [[nodiscard]] static bool IsValidEntryName(std::string_view name) noexcept;
    [[nodiscard]] static std::filesystem::path ParentVirtualPath(const std::filesystem::path& virtualPath);
    [[nodiscard]] static std::optional<std::filesystem::path> ResolveMountedFolderRoot(
        const AssetMountTable& mounts,
        const std::filesystem::path& virtualFolder);
    [[nodiscard]] static bool IsMountRoot(const std::filesystem::path& virtualPath);
    [[nodiscard]] static bool IsSameOrDescendantVirtualPath(
        const std::filesystem::path& parent,
        const std::filesystem::path& candidate);
    [[nodiscard]] static std::filesystem::path ResolvePhysicalPath(
        const AssetMountTable& mounts,
        const AssetMetadata& metadata);
    [[nodiscard]] static bool IsMountedVirtualPath(
        const AssetMountTable& mounts,
        const std::filesystem::path& virtualPath);
};

} // namespace kb::assets
