#include "engine/visual/VisualGraphRuntimeRegistry.hpp"

#include <utility>

namespace kb::visual {

void VisualGraphRuntimeRegistry::Store(VisualGraphRuntimeArtifact artifact) {
    if (!artifact.assetId.IsValid()) {
        return;
    }
    artifacts_[artifact.assetId.value] = std::move(artifact);
}

const VisualGraphRuntimeArtifact* VisualGraphRuntimeRegistry::Find(kb::assets::AssetId assetId) const noexcept {
    const auto iter = artifacts_.find(assetId.value);
    return iter == artifacts_.end() ? nullptr : &iter->second;
}

VisualGraphRuntimeArtifact* VisualGraphRuntimeRegistry::FindMutable(kb::assets::AssetId assetId) noexcept {
    const auto iter = artifacts_.find(assetId.value);
    return iter == artifacts_.end() ? nullptr : &iter->second;
}

bool VisualGraphRuntimeRegistry::Contains(kb::assets::AssetId assetId) const noexcept {
    return Find(assetId) != nullptr;
}

std::size_t VisualGraphRuntimeRegistry::Count() const noexcept {
    return artifacts_.size();
}

void VisualGraphRuntimeRegistry::Remove(kb::assets::AssetId assetId) noexcept {
    artifacts_.erase(assetId.value);
}

void VisualGraphRuntimeRegistry::Clear() noexcept {
    artifacts_.clear();
}

} // namespace kb::visual
