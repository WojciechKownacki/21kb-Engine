#pragma once

#include "kb/render/resources/RenderResources.hpp"

#include <string>

namespace kb::editor {

/// Material-domain presentation helpers shared across the asset Inspector and the
/// dedicated Material Editor panel. Avoids duplicating material value formatting.
class MaterialAssetFormatter {
public:
    MaterialAssetFormatter() = delete;

    /// Display name for a material alpha mode ("Opaque", "Mask", "Blend").
    [[nodiscard]] static std::string AlphaModeName(kb::render::RenderMaterialAlphaMode mode) noexcept;
};

} // namespace kb::editor
