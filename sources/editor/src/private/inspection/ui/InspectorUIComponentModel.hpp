#pragma once

#include "engine/scene/SceneEntity.hpp"
#include "engine/ui/UIComponentPropertyCatalog.hpp"
#include "inspection/InspectorPanelState.hpp"

#include <optional>
#include <span>
#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace kb::scene {
class Scene;
}

namespace kb::editor {

struct InspectorUIPropertyRow {
    std::string_view name;
    std::string label;
    kb::scene::UIComponentPropertyType type = kb::scene::UIComponentPropertyType::Float;
    std::string value;
    bool boolValue = false;
    bool writable = true;
    int groupStart = 0;
    int fieldCount = 1;
    bool color = false;
    std::array<float, 4> rgba{};
    std::vector<std::string_view> choices;
    // "On Click": not a property of the engine catalog but the pair eventName + eventTarget, chosen from the
    // project scripts; `value` is its text.
    bool onClick = false;
    // Rare settings, listed after the others under a collapsed "Advanced" row.
    bool advanced = false;
    // Written after the number.
    std::string_view unit;
};

class InspectorUIComponentModel final {
public:
    InspectorUIComponentModel() = delete;

    [[nodiscard]] static std::vector<kb::scene::UIComponentType> Components(
        const kb::scene::Scene& scene, kb::scene::SceneEntity entity);
    [[nodiscard]] static std::vector<InspectorUIPropertyRow> Properties(
        const kb::scene::Scene& scene, kb::scene::SceneEntity entity,
        kb::scene::UIComponentType component);

    // The component's name in the editor language; the engine's catalog name stays English for files
    // and tools.
    // Selectable settings most widgets never touch (raycast, navigation, event name and target, ...): they follow
    // the others under an "Advanced" row.
    [[nodiscard]] static bool IsAdvancedProperty(kb::scene::UIComponentType component, std::string_view name) noexcept;
    // The catalog name of the property a row of Properties() stands for ("onClick" for the Selectable's first row);
    // empty past the end.
    [[nodiscard]] static std::string_view PropertyNameAt(kb::scene::UIComponentType component, int index);
    [[nodiscard]] static std::string_view DisplayName(kb::scene::UIComponentType component);
    [[nodiscard]] static std::string_view PresetDisplayName(kb::scene::UIComponentPreset preset);
    [[nodiscard]] static InspectorSectionId Section(kb::scene::UIComponentType component) noexcept;
    [[nodiscard]] static InspectorPropertyId Property(kb::scene::UIComponentType component) noexcept;
    [[nodiscard]] static std::optional<kb::scene::UIComponentType> Component(
        InspectorSectionId section) noexcept;
    [[nodiscard]] static std::optional<kb::scene::UIComponentType> Component(
        InspectorPropertyId property) noexcept;

    // What an entity-reference field shows: "(none)" for an empty link, the target's hierarchy name,
    // or an explicit marker when the stored id no longer names something the link can use - the
    // target was deleted, or it cannot take focus - so a broken link is visible, not a bare number.
    [[nodiscard]] static std::string EntityReferenceLabel(const kb::scene::Scene& scene, std::uint64_t id,
        bool requireSelectable = false);
    // Every live object a UI navigation link may point at from `source`: interactable UI widgets,
    // in hierarchy order, excluding the source itself.
    [[nodiscard]] static std::vector<kb::scene::SceneEntity> NavigationTargets(
        const kb::scene::Scene& scene, kb::scene::SceneEntity source);
    // The objects an entity-valued UI property may point at, with the picker's title and description.
    // Navigation links take focusable widgets; a dropdown's template takes its children, its text parts
    // take Text widgets and its image parts Image widgets under it; a toggle's graphic and a selectable's
    // target graphic take any widget that draws; a scroll view takes scrollbars.
    struct ReferenceChoice {
        std::vector<kb::scene::SceneEntity> targets;
        std::string title;
        std::string description;
    };
    [[nodiscard]] static ReferenceChoice ReferenceTargets(const kb::scene::Scene& scene, kb::scene::SceneEntity source,
        kb::scene::UIComponentType component, std::string_view property);
    // "Canvas / Menu / Play" - the name path that tells two objects with the same name apart.
    [[nodiscard]] static std::string HierarchyPath(const kb::scene::Scene& scene, kb::scene::SceneEntity entity);

    [[nodiscard]] static std::optional<kb::scene::UIComponentPropertyValue> Parse(
        kb::scene::UIComponentPropertyType type, std::string_view text);
    [[nodiscard]] static std::array<InspectorUIPropertyRow, 4> RectLayoutFields(
        const kb::scene::UIRectTransform& rect);
    [[nodiscard]] static bool EditRectLayout(kb::scene::UIRectTransform& rect, int field, float value) noexcept;
    [[nodiscard]] static int AnchorPreset(const kb::scene::UIRectTransform& rect) noexcept;
    [[nodiscard]] static bool ApplyAnchorPreset(kb::scene::UIRectTransform& rect,
        int preset, kb::math::Vec2 parentSize, bool alignPosition, bool alignPivot) noexcept;
};

} // namespace kb::editor
