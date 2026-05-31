#include "assets/AssetPathUtilities.hpp"

#include <algorithm>
#include <cctype>

namespace kb::assets {

std::string AssetPathUtilities::LowerExtension(const std::filesystem::path& extension) {
    std::string text = extension.string();
    std::ranges::transform(text, text.begin(), [](unsigned char value) {
        return static_cast<char>(std::tolower(value));
    });
    return text;
}

bool AssetPathUtilities::IsValidEntryName(std::string_view name) noexcept {
    if (name.empty() || name == "." || name == "..") {
        return false;
    }
    return name.find('/') == std::string_view::npos && name.find('\\') == std::string_view::npos;
}

std::filesystem::path AssetPathUtilities::ParentVirtualPath(const std::filesystem::path& virtualPath) {
    const std::string normalized = NormalizeAssetPath(virtualPath);
    const std::size_t separator = normalized.find_last_of('/');
    if (separator == std::string::npos || separator == 0) {
        return {};
    }
    return std::filesystem::path{ normalized.substr(0, separator) };
}

std::optional<std::filesystem::path> AssetPathUtilities::ResolveMountedFolderRoot(
    const AssetMountTable& mounts,
    const std::filesystem::path& virtualFolder) {
    const std::string normalized = NormalizeAssetPath(virtualFolder);
    for (const AssetMount& mount : mounts.Mounts()) {
        if (normalized == "/" + mount.name) {
            return mount.root;
        }
    }
    return mounts.Resolve(virtualFolder);
}

bool AssetPathUtilities::IsMountRoot(const std::filesystem::path& virtualPath) {
    return NormalizeAssetPath(virtualPath).find_last_of('/') == 0;
}

bool AssetPathUtilities::IsSameOrDescendantVirtualPath(
    const std::filesystem::path& parent,
    const std::filesystem::path& candidate) {
    const std::string parentText = NormalizeAssetPath(parent);
    const std::string candidateText = NormalizeAssetPath(candidate);
    return candidateText == parentText || candidateText.starts_with(parentText + "/");
}

std::filesystem::path AssetPathUtilities::ResolvePhysicalPath(
    const AssetMountTable& mounts,
    const AssetMetadata& metadata) {
    if (!metadata.physicalPath.empty()) {
        return metadata.physicalPath;
    }
    std::optional<std::filesystem::path> resolved = mounts.Resolve(metadata.virtualPath);
    return resolved.value_or(std::filesystem::path{});
}

bool AssetPathUtilities::IsMountedVirtualPath(
    const AssetMountTable& mounts,
    const std::filesystem::path& virtualPath) {
    const std::string normalized = NormalizeAssetPath(virtualPath);
    for (const AssetMount& mount : mounts.Mounts()) {
        const std::string prefix = "/" + mount.name;
        if (normalized == prefix || normalized.starts_with(prefix + "/")) {
            return true;
        }
    }
    return false;
}

} // namespace kb::assets
