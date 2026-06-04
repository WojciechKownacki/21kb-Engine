#include "resources/RenderMeshObjMaterialResolver.hpp"

#include <algorithm>
#include <iterator>
#include <string>

namespace kb::render {
namespace {

[[nodiscard]] std::uint64_t MaterialAssetIdForName(std::string_view materialName, const RenderMeshObjImportDesc& desc) noexcept {
    for (std::uint32_t bindingIndex = 0U; bindingIndex < desc.materialBindingCount; ++bindingIndex) {
        const RenderMeshAssetMaterialBinding& binding = desc.materialBindings[bindingIndex];
        if (binding.materialName == materialName) {
            return binding.materialAssetId;
        }
    }
    return 0U;
}

} // namespace

std::uint32_t RenderMeshObjMaterialResolver::EnsureMaterialSlot(
    RenderMeshAssetData& asset,
    std::string_view materialName,
    const RenderMeshObjImportDesc& desc) {
    const auto iterator = std::ranges::find_if(asset.materialNames, [materialName](const std::string& name) {
        return name == materialName;
    });
    if (iterator != asset.materialNames.end()) {
        return static_cast<std::uint32_t>(std::distance(asset.materialNames.begin(), iterator));
    }

    asset.materialNames.push_back(std::string{ materialName });
    asset.materialSlots.push_back(RenderMaterialSlotDesc{
        .defaultMaterialAssetId = MaterialAssetIdForName(materialName, desc),
    });
    return static_cast<std::uint32_t>(asset.materialSlots.size() - 1U);
}

} // namespace kb::render
