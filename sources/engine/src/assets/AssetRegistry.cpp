#include "engine/assets/AssetRegistry.hpp"

#include <algorithm>

namespace kb::assets {

bool AssetRegistry::Upsert(AssetMetadata metadata) {
    if (!metadata.id.IsValid() || metadata.type.empty() || metadata.virtualPath.empty()) {
        return false;
    }

    metadata.virtualPath = NormalizeAssetPath(metadata.virtualPath);
    if (metadata.name.empty()) {
        metadata.name = metadata.virtualPath.stem().string();
    }

    const auto existing = byId_.find(metadata.id.value);
    if (existing != byId_.end()) {
        byPath_.erase(NormalizeAssetPath(assets_[existing->second].virtualPath));
        assets_[existing->second] = std::move(metadata);
        byPath_[NormalizeAssetPath(assets_[existing->second].virtualPath)] = assets_[existing->second].id.value;
        ++generation_;
        return true;
    }

    byPath_[NormalizeAssetPath(metadata.virtualPath)] = metadata.id.value;
    byId_[metadata.id.value] = assets_.size();
    assets_.push_back(std::move(metadata));
    ++generation_;
    return true;
}

bool AssetRegistry::Remove(AssetId id) noexcept {
    const auto iterator = byId_.find(id.value);
    if (iterator == byId_.end()) {
        return false;
    }

    const std::size_t index = iterator->second;
    byPath_.erase(NormalizeAssetPath(assets_[index].virtualPath));
    byId_.erase(iterator);

    if (index != assets_.size() - 1) {
        assets_[index] = std::move(assets_.back());
        byId_[assets_[index].id.value] = index;
        byPath_[NormalizeAssetPath(assets_[index].virtualPath)] = assets_[index].id.value;
    }
    assets_.pop_back();
    ++generation_;
    return true;
}

const AssetMetadata* AssetRegistry::Find(AssetId id) const noexcept {
    const auto iterator = byId_.find(id.value);
    return iterator == byId_.end() ? nullptr : &assets_[iterator->second];
}

AssetMetadata* AssetRegistry::FindMutable(AssetId id) noexcept {
    const auto iterator = byId_.find(id.value);
    return iterator == byId_.end() ? nullptr : &assets_[iterator->second];
}

const AssetMetadata* AssetRegistry::FindByPath(const std::filesystem::path& virtualPath) const noexcept {
    const auto iterator = byPath_.find(NormalizeAssetPath(virtualPath));
    return iterator == byPath_.end() ? nullptr : Find(AssetId{ iterator->second });
}

std::vector<AssetMetadata> AssetRegistry::ByType(std::string_view type) const {
    std::vector<AssetMetadata> output;
    for (const AssetMetadata& metadata : assets_) {
        if (metadata.type == type) {
            output.push_back(metadata);
        }
    }
    return output;
}

std::span<const AssetMetadata> AssetRegistry::All() const noexcept {
    return assets_;
}

std::size_t AssetRegistry::Count() const noexcept {
    return assets_.size();
}

void AssetRegistry::Clear() noexcept {
    assets_.clear();
    byId_.clear();
    byPath_.clear();
    ++generation_;
}

} // namespace kb::assets
