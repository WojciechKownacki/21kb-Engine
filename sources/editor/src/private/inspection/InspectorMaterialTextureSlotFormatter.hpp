#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace kb::assets {
class AssetManager;
}

namespace kb::editor {

class InspectorMaterialTextureSlotFormatter {
public:
    InspectorMaterialTextureSlotFormatter() = delete;

    [[nodiscard]] static std::string DisplayName(const kb::assets::AssetManager& manager, std::uint64_t assetId);
    [[nodiscard]] static bool IsMissing(const kb::assets::AssetManager& manager, std::uint64_t assetId) noexcept;
    [[nodiscard]] static std::string Diagnostic(std::string_view slotName, std::uint64_t assetId);
};

} // namespace kb::editor
