#pragma once

#include "app/scene_viewport/EditorSceneViewportTypes.hpp"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif

namespace kb::editor {

#if defined(_WIN32)

class EditorSceneViewportGizmoHitTester {
public:
    EditorSceneViewportGizmoHitTester() = delete;

    [[nodiscard]] static float ScreenSpaceScale(
        const EditorViewportCameraState& camera,
        const RECT& renderArea,
        kb::scene::Vec3 targetPosition) noexcept;

    [[nodiscard]] static int HitAxis(
        const EditorViewportCameraState& camera,
        const RECT& renderArea,
        kb::scene::Vec3 targetPosition,
        float worldScale,
        float localX,
        float localY) noexcept;

    [[nodiscard]] static int HitRotationAxis(
        const EditorViewportCameraState& camera,
        const RECT& renderArea,
        kb::scene::Vec3 targetPosition,
        float worldScale,
        float localX,
        float localY) noexcept;

    [[nodiscard]] static bool HitCenter(
        const EditorViewportCameraState& camera,
        const RECT& renderArea,
        kb::scene::Vec3 targetPosition,
        float localX,
        float localY) noexcept;
};

#endif

} // namespace kb::editor
