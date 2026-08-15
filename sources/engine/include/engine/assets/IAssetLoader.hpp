#pragma once

#include "engine/assets/AssetMetadata.hpp"

#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <typeindex>
#include <vector>

namespace kb::assets {

class AssetRegistry;

struct AssetLoadRequest {
    const AssetMetadata& metadata;
    std::filesystem::path resolvedPath;
};

struct AssetLoadResult {
    std::shared_ptr<void> asset{};
    std::string error;

    [[nodiscard]] bool Succeeded() const noexcept {
        return asset != nullptr && error.empty();
    }
};

class IAssetLoader {
public:
    virtual ~IAssetLoader() = default;

    [[nodiscard]] virtual std::string_view Type() const noexcept = 0;
    [[nodiscard]] virtual std::type_index PayloadType() const noexcept = 0;
    [[nodiscard]] virtual std::vector<std::string> Extensions() const = 0;
    [[nodiscard]] virtual AssetLoadResult Load(const AssetLoadRequest& request) = 0;
    [[nodiscard]] virtual std::vector<AssetId> DiscoverDependencies(const AssetMetadata& metadata, const AssetRegistry& registry) const {
        static_cast<void>(metadata);
        static_cast<void>(registry);
        return {};
    }
    [[nodiscard]] virtual std::optional<std::string> ValidateDependencies(
        const AssetMetadata& metadata,
        const AssetRegistry& registry) const {
        static_cast<void>(metadata);
        static_cast<void>(registry);
        return std::nullopt;
    }
    // Discovery-time-only free-text tag a loader can offer for its own asset type (e.g. a particle
    // recipe's authored category), stored into AssetMetadata.browseTag - a search-only field, NOT
    // the closed importCategory vocabulary - so browsers can search/filter on it without ever
    // calling the full, cache-mutating Load() per row. Skipped when the file's content hash is
    // unchanged since the previous scan (see AssetDiscoveryService), so it is not necessarily
    // "cheap" per call for loaders backed by a full document parse, but its steady-state cost is
    // bounded to actual file changes rather than every scan. Default is empty (no tag), matching
    // every loader that has nothing to offer here.
    [[nodiscard]] virtual std::string DiscoverBrowseTag(const std::filesystem::path& path) const {
        static_cast<void>(path);
        return {};
    }
};

} // namespace kb::assets
