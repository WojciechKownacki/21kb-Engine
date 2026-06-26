#pragma once

#include "kb/render/resources/RenderResources.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace kb::editor {

struct MaterialDebugChannelRow {
    std::string label;
    std::string value;
};

/// Material-domain presentation helpers shared across the asset Inspector and the
/// dedicated Material Editor panel. Avoids duplicating material value formatting.
class MaterialAssetFormatter {
public:
    MaterialAssetFormatter() = delete;

    /// Display name for a material alpha mode ("Opaque", "Mask", "Blend").
    [[nodiscard]] static std::string AlphaModeName(kb::render::RenderMaterialAlphaMode mode) noexcept;
    /// Artist-facing debug channel rows for the currently inspected PBR material.
    [[nodiscard]] static std::vector<MaterialDebugChannelRow> DebugChannelRows(
        const kb::render::RenderMaterialDesc& material,
        std::uint64_t materialAssetId);
};

} // namespace kb::editor
