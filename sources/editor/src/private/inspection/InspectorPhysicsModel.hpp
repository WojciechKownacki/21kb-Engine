#pragma once

#include "engine/scene/CharacterControllerComponent.hpp"
#include "engine/scene/ColliderComponent.hpp"
#include "engine/scene/JointComponent.hpp"
#include "engine/scene/RigidbodyComponent.hpp"

#include <array>
#include <cstdint>
#include <optional>
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
    ReadOnly,
};

struct PhysicsField {
    std::string label;
    PhysicsFieldKind kind = PhysicsFieldKind::Float;
    std::string value;      // display text (formatted value, enum name, or unused for Bool)
    bool boolValue = false; // meaningful only when kind == Bool
    // What the row tells the author beyond its label: one plain sentence for the hover help, the unit shown
    // after a number, and, for a number with a natural range, that range (the row then draws as a slider).
    std::string tooltip;
    std::string_view unit;
    float number = 0.0F; // the value of a Float row
    bool bounded = false;
    float minimum = 0.0F;
    float maximum = 0.0F;
};

// The rows of one physics component as the Inspector lays them out: field indices in display order. The first
// `basicCount` of them are always shown; the others sit behind the section's Advanced row. A section whose
// rows are all basic (`basicCount == count`) has no Advanced row. Paint, hit testing and the section height
// all read this one list, so they cannot disagree about which rows exist.
struct PhysicsLayout {
    static constexpr int kMaxRows = 64;
    std::array<std::uint8_t, kMaxRows> rows{};
    int count = 0;
    int basicCount = 0;

    [[nodiscard]] bool HasAdvanced() const noexcept { return basicCount < count; }
};

class InspectorPhysicsModel {
public:
    // How many rows a component shows. Every list below is a fixed one, so the count
    // is known without building a single string - and both the panel's height and its
    // hit testing want only the count. They used to build the whole display list for
    // it, on every repaint and on every mouse move across the panel.
    [[nodiscard]] static constexpr int FieldCount(PhysicsComponentKind component) noexcept {
        switch (component) {
        case PhysicsComponentKind::Rigidbody: return 14;
        case PhysicsComponentKind::Collider: return 15;
        case PhysicsComponentKind::CharacterController: return 9;
        case PhysicsComponentKind::Joint: return JointRowCount;
        }
        return 0;
    }

    // Joint rows: the shared ones first (index 0 is the type), then the settings of the slider, cone and
    // six-degrees-of-freedom kinds. Which of them the Inspector shows, and in what order, is Layout().
    static constexpr int JointSecondaryAxisRow = 18; // three rows: x, y, z
    static constexpr int JointSwing1Row = 21;        // then the second swing limit
    static constexpr int JointFirstAxisRow = 23;     // JointAxisRows rows per axis, JointAxisCount axes
    static constexpr int JointAxisRows = 5;          // mode, min, max, drive velocity, drive force
    static constexpr int JointAxisCount = static_cast<int>(kb::scene::kJointAxisCount);
    // Plain rows after the raw ones. Each stands for one or two raw values and is shown only for the kind that
    // reads them: how far a hinge opens (either way), how far a slider travels, how long a rope is, how far a
    // cone tilts and twists, the motor speed, and the direction as a choice of the X, Y or Z axis.
    static constexpr int JointPlainFirstRow = JointFirstAxisRow + JointAxisRows * JointAxisCount;
    static constexpr int JointOpensRow = JointPlainFirstRow;
    static constexpr int JointSlidesRow = JointPlainFirstRow + 1;
    static constexpr int JointLengthRow = JointPlainFirstRow + 2;
    static constexpr int JointTiltRow = JointPlainFirstRow + 3;
    static constexpr int JointTwistRow = JointPlainFirstRow + 4;
    static constexpr int JointMotorSpeedRow = JointPlainFirstRow + 5;
    static constexpr int JointDirectionRow = JointPlainFirstRow + 6;
    static constexpr int JointRowCount = JointPlainFirstRow + 7;

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

    // Which rows a component shows and in what order (see PhysicsLayout). Rows that mean nothing for the
    // component's current setting (a box size on a sphere, a motor on a fixed joint) are left out.
    [[nodiscard]] static PhysicsLayout Layout(const kb::scene::RigidbodyComponent& component) noexcept;
    [[nodiscard]] static PhysicsLayout Layout(const kb::scene::ColliderComponent& component) noexcept;
    [[nodiscard]] static PhysicsLayout Layout(const kb::scene::CharacterControllerComponent& component) noexcept;
    [[nodiscard]] static PhysicsLayout Layout(const kb::scene::JointComponent& component) noexcept;

    // The editable rows whose value differs from a freshly added component's: the rows the Inspector marks
    // so the author can put them back. Read-only rows never appear.
    [[nodiscard]] static std::vector<int> ChangedRows(const kb::scene::RigidbodyComponent& component);
    [[nodiscard]] static std::vector<int> ChangedRows(const kb::scene::ColliderComponent& component);
    [[nodiscard]] static std::vector<int> ChangedRows(const kb::scene::CharacterControllerComponent& component);
    [[nodiscard]] static std::vector<int> ChangedRows(const kb::scene::JointComponent& component);

    // Puts one row back to a freshly added component's value. False for a read-only or out-of-range row.
    [[nodiscard]] static bool ResetRow(kb::scene::RigidbodyComponent& component, int index);
    [[nodiscard]] static bool ResetRow(kb::scene::ColliderComponent& component, int index);
    [[nodiscard]] static bool ResetRow(kb::scene::CharacterControllerComponent& component, int index);
    [[nodiscard]] static bool ResetRow(kb::scene::JointComponent& component, int index);
};

// Joint setups chosen from the Preset row: the joint type and the limits that suit a common use. Anchors, the
// connected body, motor, spring and break force stay as the author set them.
enum class JointPreset {
    Door,        // hinge turning about the up axis, a quarter turn either way
    SlidingDoor, // slider along X, from the closed pose out to 1.5 m
    Pendulum,    // hinge about Z with no limit, free to swing
    Rope,        // rope up to 3 m long
    CharacterLimb, // cone: 45 degrees of swing, a 45 degree twist either way
};

inline constexpr int kJointPresetCount = 5;

[[nodiscard]] std::string_view JointPresetName(JointPreset preset);
void ApplyJointPreset(kb::scene::JointComponent& joint, JointPreset preset) noexcept;

// The preset the joint's settings amount to, so the Preset row can name it; none once any value a preset sets
// has been changed ("Custom").
[[nodiscard]] std::optional<JointPreset> MatchJointPreset(const kb::scene::JointComponent& joint) noexcept;

} // namespace kb::editor
