#include "inspection/InspectorMaterialTextureSlotFormatter.hpp"

#include "engine/assets/AssetManager.hpp"

namespace kb::editor {

std::string InspectorMaterialTextureSlotFormatter::DisplayName(const kb::assets::AssetManager& manager, std::uint64_t assetId) {
    if (assetId == 0U) {
        return "None";
    }
    const kb::assets::AssetMetadata* metadata = manager.Registry().Find(kb::assets::AssetId{ assetId });
    if (metadata == nullptr) {
        return "Missing texture asset " + std::to_string(assetId);
    }
    if (!metadata->name.empty()) {
        return metadata->name;
    }
    return metadata->virtualPath.filename().string();
}

bool InspectorMaterialTextureSlotFormatter::IsMissing(const kb::assets::AssetManager& manager, std::uint64_t assetId) noexcept {
    return assetId != 0U && manager.Registry().Find(kb::assets::AssetId{ assetId }) == nullptr;
}

std::string InspectorMaterialTextureSlotFormatter::Diagnostic(std::string_view slotName, std::uint64_t assetId) {
    return std::string{ slotName } + " texture references missing asset " + std::to_string(assetId);
}

} // namespace kb::editor
