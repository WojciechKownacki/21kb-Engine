#pragma once

#include "rendering/MaterialPreviewAppearanceResolver.hpp"

namespace kb::editor {

#if defined(_WIN32)

// Editor-side supplier for MaterialPreviewAppearanceResolver: averages the decoded texture thumbnail the
// preview service already keeps, so a texture-driven material reads as its dominant colour instead of white.
[[nodiscard]] std::optional<std::array<float, 3U>> MaterialPreviewTextureAverageColor(
    const kb::assets::AssetManager& assets,
    kb::assets::AssetId textureId);

#endif

} // namespace kb::editor
