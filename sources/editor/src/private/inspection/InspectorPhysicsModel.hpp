#pragma once

#include "engine/scene/CharacterControllerComponent.hpp"
#include "engine/scene/ColliderComponent.hpp"
#include "engine/scene/JointComponent.hpp"
#include "engine/scene/RigidbodyComponent.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace kb::editor {

// The four physics components (Rigidbody, Collider, Character Controller, Joint)
// carry many mixed-type fields. Rather than mint one InspectorPropertyId per
// field, the Inspector renders each component as an index-addressed list of
// PhysicsField rows (the same "one property id + row index" model the exposed
// script variables and input mappings already use). This pure model owns the
// field layout and the read/edit rules; EditorSceneContext wraps it in undoable
// transactions and the panel renders/hit-tests it. Kept free of any editor state
// so it is unit-testable against the raw component structs.
enum class PhysicsComponentKind {
    Rigidbody,
    Collider,
    CharacterController,
    Joint,
};

enum class PhysicsFieldKind {
    Float, // inline text edit, parsed as a number
    Bool,  // checkbox toggle
    Enum,  // click cycles to the next enumerator
};

struct PhysicsField {
    std::string label;
    PhysicsFieldKind kind = PhysicsFieldKind::Float;
    std::string value;      // display text (formatted number, enum name, or unused for Bool)
    bool boolValue = false; // meaningful only when kind == Bool
};

class InspectorPhysicsModel {
public:
    // The index-stable field rows for a component, in render order.
    [[nodiscard]] static std::vector<PhysicsField> Fields(const kb::scene::RigidbodyComponent& component);
    [[nodiscard]] static std::vector<PhysicsField> Fields(const kb::scene::ColliderComponent& component);
    [[nodiscard]] static std::vector<PhysicsField> Fields(const kb::scene::CharacterControllerComponent& component);
    [[nodiscard]] static std::vector<PhysicsField> Fields(const kb::scene::JointComponent& component);

    // The PhysicsFieldKind of a given row (so callers know how to interact with a
    // hit) without materializing the full display list. Returns Float for an
    // out-of-range index (harmless — the edit paths re-validate).
    [[nodiscard]] static PhysicsFieldKind KindOf(PhysicsComponentKind component, int index) noexcept;

    // Commit a text edit into the numeric field at `index`. `parsedValue` is the
    // already-evaluated number (the caller runs the shared math evaluator so
    // "+2"/"*3" work exactly like every other float field). No-op + false for a
    // non-Float field or an out-of-range index.
    [[nodiscard]] static bool ApplyFloat(kb::scene::RigidbodyComponent& component, int index, float parsedValue) noexcept;
    [[nodiscard]] static bool ApplyFloat(kb::scene::ColliderComponent& component, int index, float parsedValue) noexcept;
    [[nodiscard]] static bool ApplyFloat(kb::scene::CharacterControllerComponent& component, int index, float parsedValue) noexcept;
    [[nodiscard]] static bool ApplyFloat(kb::scene::JointComponent& component, int index, float parsedValue) noexcept;

    // The current numeric value of a Float field (seed for the inline editor).
    [[nodiscard]] static bool ReadFloat(const kb::scene::RigidbodyComponent& component, int index, float& out) noexcept;
    [[nodiscard]] static bool ReadFloat(const kb::scene::ColliderComponent& component, int index, float& out) noexcept;
    [[nodiscard]] static bool ReadFloat(const kb::scene::CharacterControllerComponent& component, int index, float& out) noexcept;
    [[nodiscard]] static bool ReadFloat(const kb::scene::JointComponent& component, int index, float& out) noexcept;

    // Toggle the Bool field at `index`. False for a non-Bool / out-of-range index.
    [[nodiscard]] static bool ToggleBool(kb::scene::RigidbodyComponent& component, int index) noexcept;
    [[nodiscard]] static bool ToggleBool(kb::scene::ColliderComponent& component, int index) noexcept;
    [[nodiscard]] static bool ToggleBool(kb::scene::CharacterControllerComponent& component, int index) noexcept;
    [[nodiscard]] static bool ToggleBool(kb::scene::JointComponent& component, int index) noexcept;

    // Advance the Enum field at `index` to the next enumerator (wraps). False for
    // a non-Enum / out-of-range index.
    [[nodiscard]] static bool CycleEnum(kb::scene::RigidbodyComponent& component, int index) noexcept;
    [[nodiscard]] static bool CycleEnum(kb::scene::ColliderComponent& component, int index) noexcept;
    [[nodiscard]] static bool CycleEnum(kb::scene::CharacterControllerComponent& component, int index) noexcept;
    [[nodiscard]] static bool CycleEnum(kb::scene::JointComponent& component, int index) noexcept;
};

} // namespace kb::editor
