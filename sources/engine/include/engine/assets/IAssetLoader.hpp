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
    // Cheap, discovery-time-only classification tag a loader can offer for its own asset type
    // (e.g. a particle recipe's authored category), stored into AssetMetadata.importCategory so
    // browsers can search/filter on it without ever calling the full, cache-mutating Load() per
    // row. Called once per file per discovery scan, not on any per-frame/per-row path. Default is
    // empty (no tag), matching every loader that has nothing cheap to offer here.
    [[nodiscard]] virtual std::string DiscoverImportCategory(const std::filesystem::path& path) const {
        static_cast<void>(path);
        return {};
    }
};

} // namespace kb::assets
