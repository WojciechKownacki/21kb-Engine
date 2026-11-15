#pragma once

#include "engine/scene/Scene.hpp"
#include "inspection/InspectorPanelState.hpp"

#include <vector>

namespace kb::editor {

// What "the default" of a component's settings is, for the components the Inspector offers to reset, copy and paste
// (Camera, Light, Mesh Renderer, Rigidbody, Collider, Character Controller, Joint, Audio Source, Particle Effect): the
// component as Add Component creates it. The Transform has the same marks and resets but no Copy, Paste or Remove; its
// default is the origin, no rotation and a scale of one.
// Everything works on the scene directly; the caller wraps a call in an undoable scene command.
class InspectorComponentDefaults {
public:
    InspectorComponentDefaults() = delete;

    [[nodiscard]] static bool Supports(InspectorSectionId section) noexcept;

    // The fields of the entity's components whose value differs from the default, so the Inspector can mark them.
    // Asset references (a mesh, a material) are choices, not values, and never appear.
    [[nodiscard]] static std::vector<InspectorFieldKey> ChangedFields(const kb::scene::Scene& scene, kb::scene::SceneEntity entity);

    // Puts one field back to the default. False when the field has none or the entity lacks the component.
    [[nodiscard]] static bool ResetField(kb::scene::Scene& scene, kb::scene::SceneEntity entity, const InspectorFieldKey& field);

    // Puts every value of one component back to the default; asset references and the connected body stay.
    [[nodiscard]] static bool ResetComponent(kb::scene::Scene& scene, kb::scene::SceneEntity entity, InspectorSectionId section);

    // The Transform's menu: puts the position, the rotation or the scale back (the entries of InspectorComponentMenuItems).
    [[nodiscard]] static bool ResetTransformPart(kb::scene::Scene& scene, kb::scene::SceneEntity entity, InspectorComponentMenuItem part);

    // Copy and paste of one component's settings, held for the editor session. A paste keeps the target's own
    // connected body and runtime material handle.
    [[nodiscard]] static bool CopyComponent(const kb::scene::Scene& scene, kb::scene::SceneEntity entity, InspectorSectionId section);
    [[nodiscard]] static bool CanPaste(InspectorSectionId section) noexcept;
    [[nodiscard]] static bool PasteComponent(kb::scene::Scene& scene, kb::scene::SceneEntity entity, InspectorSectionId section);
};

} // namespace kb::editor
