#pragma once

#include "engine/assets/AssetId.hpp"
#include "kb/editor/theme/EditorTheme.hpp"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif

#include <functional>

namespace kb::editor {

class EditorSceneContext;
class EditorSceneBgfxViewport;

class EditorParticleEffectAssetPickerDialog {
public:
    EditorParticleEffectAssetPickerDialog() = delete;

#if defined(_WIN32)
    using AcceptedCallback = std::function<void(kb::assets::AssetId)>;

    [[nodiscard]] static bool Open(
        HWND owner,
        const EditorTheme& theme,
        EditorSceneContext& sceneContext,
        EditorSceneBgfxViewport& sceneViewport,
        kb::assets::AssetId currentEffect,
        AcceptedCallback onAccepted);
#endif
};

} // namespace kb::editor
