#pragma once

#include "engine/scene/TransformComponent.hpp"

#include <vector>

namespace kb::scene {

class Scene;

// LIB-132: a single 3D line segment describing part of a physics debug-draw wireframe
// (collider/character-controller/joint shapes, or the single most-recent query trace).
// Deliberately renderer-independent (kb::engine owns no compile/link-time dependency on any
// renderer SDK, mirroring the same boundary kb::engine already keeps against physics SDKs -
// see IPhysicsBackend's own doc comment) - whichever host actually draws these (the editor's
// Scene Viewport today) converts this into its own renderer-facing line type each frame.
struct PhysicsDebugLineDesc {
    Vec3 from{};
    Vec3 to{};
    Vec3 color{1.0F, 1.0F, 1.0F};
    float alpha = 1.0F;
};

// LIB-132: "trace pojedynczego query" - literally ONE query, not a history/log. Recorded by
// PhysicsDebugDraw::RecordQueryTrace (called from ScriptPhysicsApi.cpp's Raycast/*Cast/
// Overlap* functions) whenever debug draw is enabled, overwritten by the next such call.
struct PhysicsDebugQueryTrace {
    bool valid = false;
    bool hit = false;
    Vec3 origin{};
    Vec3 endpoint{}; // The hit point when hit is true, otherwise origin + direction*maxDistance.
    Vec3 normal{};   // Only meaningful when hit is true.
};

// LIB-132: physics debug draw and single-query trace, entirely engine-side (ECS component
// data only - ColliderComponent/CharacterControllerComponent/JointComponent -, no dependency
// on any specific physics backend or the renderer). Off by default and structurally
// impossible to affect a shipped player: only the editor's own Scene Viewport code
// (ScenePanelContentRenderer.cpp) ever calls CollectLines and feeds the result into
// RenderSceneSubmitDesc::physicsDebugLines - kb_standalone_player never populates that field
// at all (see EditorRenderPassSubmitter::SubmitGizmoOverlay's existing editorSceneOverlaysEnabled
// gating, which this reuses unchanged). When disabled, RecordQueryTrace is a single branch,
// and CollectLines is never called - zero release-path cost either way.
class PhysicsDebugDraw final {
public:
    PhysicsDebugDraw() = delete;

    static void SetEnabled(Scene& scene, bool enabled) noexcept;
    [[nodiscard]] static bool IsEnabled(const Scene& scene) noexcept;

    // Honest no-op when disabled - callers (ScriptPhysicsApi.cpp) call this unconditionally,
    // the cost of "not tracing" is exactly one bool check.
    static void RecordQueryTrace(Scene& scene, PhysicsDebugQueryTrace trace) noexcept;
    [[nodiscard]] static PhysicsDebugQueryTrace QueryTrace(const Scene& scene) noexcept;

    // Builds wireframe lines for every live Collider/CharacterController/Joint component plus
    // the last recorded query trace (if any) - pure ECS-side geometry, matches whatever the
    // real physics backend (if any) actually simulates from those same components' fields.
    [[nodiscard]] static std::vector<PhysicsDebugLineDesc> CollectLines(const Scene& scene);
};

} // namespace kb::scene
