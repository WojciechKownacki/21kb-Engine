#pragma once

#include "kb/editor/docking/DockTypes.hpp"
#include "kb/editor/theme/EditorTheme.hpp"
#include "rendering/EditorRenderBackendSettings.hpp"
#include "rendering/EditorSceneBgfxViewport.hpp"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif

namespace kb::editor {

class EditorSceneContext;

class ParticleEditorPanelRenderer final {
public:
#if defined(_WIN32)
    void Paint(HDC dc, HWND host, const RECT& content, const DockPanel& panel,
               const EditorTheme& theme, const EditorSceneContext& sceneContext,
               const EditorRenderBackendSettings& renderBackendSettings,
               EditorSceneBgfxViewport* sceneViewport) const;
    [[nodiscard]] static RECT ViewportRect(const RECT& content, unsigned int dpi = 96U) noexcept;
    [[nodiscard]] static bool PresentViewport(EditorSceneBgfxViewport& viewport, HWND host,
                                              const RECT& content, const DockPanel& panel,
                                              const EditorSceneContext& sceneContext,
                                              const EditorRenderBackendSettings& settings);
#endif
};

} // namespace kb::editor
