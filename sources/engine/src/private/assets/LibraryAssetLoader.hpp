#pragma once

#include "engine/assets/IAssetLoader.hpp"

namespace kb::assets {
// This wrapper and its shared ownership blocks must be constructed by the host.
class LibraryAssetLoader final : public IAssetLoader {
public:
    LibraryAssetLoader(std::shared_ptr<void> library, std::unique_ptr<IAssetLoader> loader)
        : library_(std::move(library)), loader_(std::move(loader)) {}
    std::string_view Type() const noexcept override { return loader_->Type(); }
    std::type_index PayloadType() const noexcept override { return loader_->PayloadType(); }
    std::vector<std::string> Extensions() const override { return loader_->Extensions(); }
    std::vector<std::string> BakedAssetTypes() const override { return loader_->BakedAssetTypes(); }
    bool AllowsConcurrentLoad() const noexcept override { return loader_->AllowsConcurrentLoad(); }
    std::vector<AssetId> DiscoverDependencies(const AssetMetadata& metadata, const AssetRegistry& registry) const override {
        return loader_->DiscoverDependencies(metadata, registry);
    }
    std::optional<std::string> ValidateDependencies(const AssetMetadata& metadata, const AssetRegistry& registry) const override {
        return loader_->ValidateDependencies(metadata, registry);
    }
    std::optional<std::string> ValidateRuntimeDependencies(const AssetLoadRequest& request, const AssetRegistry& registry) const override {
        return loader_->ValidateRuntimeDependencies(request, registry);
    }
    std::string DiscoverBrowseTag(const std::filesystem::path& path) const override { return loader_->DiscoverBrowseTag(path); }
    std::shared_ptr<void> RetainPayload(std::shared_ptr<void> payload) const override {
        if (!payload) return {};
        // Reverse destruction order releases plugin payload/control block before native code.
        struct Ownership { std::shared_ptr<void> library; std::shared_ptr<void> payload; };
        auto owner = std::make_shared<Ownership>(Ownership{ library_, std::move(payload) });
        return std::shared_ptr<void>{ owner, owner->payload.get() };
    }
    AssetLoadResult Load(const AssetLoadRequest& request) override {
        auto result = loader_->Load(request);
        result.asset = RetainPayload(std::move(result.asset));
        return result;
    }
private:
    std::shared_ptr<void> library_;
    std::unique_ptr<IAssetLoader> loader_;
};
} // namespace kb::assets
