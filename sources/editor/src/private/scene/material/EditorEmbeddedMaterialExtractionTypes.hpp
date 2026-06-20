#pragma once

#include "engine/assets/AssetId.hpp"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace kb::editor {

struct EditorExtractedMaterialSlot {
    std::uint32_t slotIndex = 0;
    kb::assets::AssetId materialAssetId{};
    std::filesystem::path virtualPath;
};

struct EditorEmbeddedMaterialExtractionResult {
    std::vector<EditorExtractedMaterialSlot> slots;
    std::vector<std::string> diagnostics;

    [[nodiscard]] bool Succeeded() const noexcept {
        return !slots.empty();
    }
};

} // namespace kb::editor
