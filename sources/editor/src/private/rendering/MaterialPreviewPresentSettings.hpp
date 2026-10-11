#pragma once

#if defined(_WIN32)

#include "engine/assets/AssetId.hpp"
#include "rendering/EditorSceneBgfxViewport.hpp"

#include <cstdint>

#include <windows.h>

namespace kb::assets {
struct AssetMetadata;
}

namespace kb::editor {

class EditorSceneContext;

// The material asset a material preview shows (a material or a material instance), or null.
[[nodiscard]] const kb::assets::AssetMetadata* MaterialPreviewMetadata(
    const EditorSceneContext& sceneContext, kb::assets::AssetId assetId) noexcept;

// How the Inspector's and the Material Editor's material previews present: their camera,
// neutral lighting, stable exposure and the graph variant (node preview or material preview).
// One owner for the window paint, the message loop and headless automation.
[[nodiscard]] EditorSceneBgfxViewport::PresentSettings BuildMaterialPreviewPresentSettings(
    EditorSceneContext& sceneContext, const RECT& previewRect, std::uint64_t viewportKey);

} // namespace kb::editor

#endif
