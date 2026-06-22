#include "inspection/MaterialAssetFormatter.hpp"

namespace kb::editor {

std::string MaterialAssetFormatter::AlphaModeName(kb::render::RenderMaterialAlphaMode mode) noexcept {
    switch (mode) {
    case kb::render::RenderMaterialAlphaMode::Opaque:
        return "Opaque";
    case kb::render::RenderMaterialAlphaMode::Mask:
        return "Mask";
    case kb::render::RenderMaterialAlphaMode::Blend:
        return "Blend";
    }
    return "Opaque";
}

} // namespace kb::editor
