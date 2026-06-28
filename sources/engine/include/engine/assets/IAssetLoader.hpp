#pragma once

#include "engine/assets/AssetMetadata.hpp"

#include <filesystem>
#include <memory>
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
};

} // namespace kb::assets
