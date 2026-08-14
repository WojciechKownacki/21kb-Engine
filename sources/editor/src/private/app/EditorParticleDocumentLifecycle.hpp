#pragma once

#include "editor/ParticleDocumentCloseGuard.hpp"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif

#include <string_view>

namespace kb::editor {

class EditorSceneContext;

class EditorParticleDocumentLifecycle final {
public:
#if defined(_WIN32)
    [[nodiscard]] static bool Resolve(
        HWND owner,
        EditorSceneContext& sceneContext,
        kb::particle_editor::ParticleDocumentTransition transition,
        std::wstring_view action);
#endif
};

} // namespace kb::editor
