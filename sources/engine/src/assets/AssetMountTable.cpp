#include "engine/assets/AssetMountTable.hpp"

#include "engine/assets/AssetMetadata.hpp"

#include <algorithm>
#include <system_error>

namespace kb::assets {
namespace {

[[nodiscard]] bool StartsWithPath(const std::filesystem::path& path, const std::filesystem::path& root) {
    const std::string pathText = NormalizeAssetPath(path);
    const std::string rootText = NormalizeAssetPath(root);
    return pathText == rootText || (pathText.size() > rootText.size() && pathText.starts_with(rootText + "/"));
}

[[nodiscard]] std::filesystem::path AbsoluteNormalized(const std::filesystem::path& path) {
    std::error_code error;
    std::filesystem::path absolute = std::filesystem::weakly_canonical(path, error);
    if (error) {
        absolute = std::filesystem::absolute(path, error);
    }
    return error ? path.lexically_normal() : absolute.lexically_normal();
}

} // namespace

bool AssetMountTable::Mount(std::string name, std::filesystem::path root) {
    if (name.empty() || root.empty() || name.find('/') != std::string::npos || name.find('\\') != std::string::npos) {
        return false;
    }

    root = AbsoluteNormalized(root);
    const auto existing = std::ranges::find_if(mounts_, [&name](const AssetMount& mount) {
        return mount.name == name;
    });
    if (existing != mounts_.end()) {
        existing->root = std::move(root);
        return true;
    }

    mounts_.push_back(AssetMount{ .name = std::move(name), .root = std::move(root) });
    return true;
}

bool AssetMountTable::Unmount(std::string_view name) noexcept {
    const auto oldSize = mounts_.size();
    std::erase_if(mounts_, [name](const AssetMount& mount) {
        return mount.name == name;
    });
    return mounts_.size() != oldSize;
}

std::optional<std::filesystem::path> AssetMountTable::Resolve(const std::filesystem::path& virtualPath) const {
    const std::string text = NormalizeAssetPath(virtualPath);
    if (text.size() < 3 || text.front() != '/') {
        return std::nullopt;
    }

    const std::size_t separator = text.find('/', 1);
    if (separator == std::string::npos) {
        return std::nullopt;
    }

    const std::string mountName = text.substr(1, separator - 1);
    const auto mount = std::ranges::find_if(mounts_, [&mountName](const AssetMount& candidate) {
        return candidate.name == mountName;
    });
    if (mount == mounts_.end()) {
        return std::nullopt;
    }

    return (mount->root / text.substr(separator + 1)).lexically_normal();
}

std::optional<std::filesystem::path> AssetMountTable::ToVirtual(const std::filesystem::path& physicalPath) const {
    const std::filesystem::path absolute = AbsoluteNormalized(physicalPath);
    for (const AssetMount& mount : mounts_) {
        if (!StartsWithPath(absolute, mount.root)) {
            continue;
        }

        std::error_code error;
        std::filesystem::path relative = std::filesystem::relative(absolute, mount.root, error);
        if (error) {
            continue;
        }
        return std::filesystem::path{ "/" + mount.name } / relative;
    }
    return std::nullopt;
}

const std::vector<AssetMount>& AssetMountTable::Mounts() const noexcept {
    return mounts_;
}

void AssetMountTable::Clear() noexcept {
    mounts_.clear();
}

} // namespace kb::assets
