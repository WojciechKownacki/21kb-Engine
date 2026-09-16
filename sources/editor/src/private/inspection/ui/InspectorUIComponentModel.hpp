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
};

class InspectorUIComponentModel final {
public:
    InspectorUIComponentModel() = delete;

    [[nodiscard]] static std::vector<kb::scene::UIComponentType> Components(
        const kb::scene::Scene& scene, kb::scene::SceneEntity entity);
    [[nodiscard]] static std::vector<InspectorUIPropertyRow> Properties(
        const kb::scene::Scene& scene, kb::scene::SceneEntity entity,
        kb::scene::UIComponentType component);

    [[nodiscard]] static InspectorSectionId Section(kb::scene::UIComponentType component) noexcept;
    [[nodiscard]] static InspectorPropertyId Property(kb::scene::UIComponentType component) noexcept;
    [[nodiscard]] static std::optional<kb::scene::UIComponentType> Component(
        InspectorSectionId section) noexcept;
    [[nodiscard]] static std::optional<kb::scene::UIComponentType> Component(
        InspectorPropertyId property) noexcept;

    // What an entity-reference field shows: "(none)" for an empty link, the target's hierarchy name,
    // or an explicit marker when the stored id no longer names something the link can use - the
    // target was deleted, or it cannot take focus - so a broken link is visible, not a bare number.
    [[nodiscard]] static std::string EntityReferenceLabel(const kb::scene::Scene& scene, std::uint64_t id);
    // Every live object a UI navigation link may point at from `source`: interactable UI widgets,
    // in hierarchy order, excluding the source itself.
    [[nodiscard]] static std::vector<kb::scene::SceneEntity> NavigationTargets(
        const kb::scene::Scene& scene, kb::scene::SceneEntity source);
    // The widget types an option can show, in the order the Add Option menu offers them. Text is a plain
    // option; every other entry creates that widget as the option's content.
    struct DropdownOptionType {
        std::string_view label;
        std::optional<kb::scene::UIComponentType> content;
    };
    [[nodiscard]] static std::span<const DropdownOptionType> DropdownOptionTypes() noexcept;
    // What an option shows, for its badge: "Text", the content widget's kind, or "Missing" when the
    // linked child is gone.
    [[nodiscard]] static std::string_view DropdownOptionKind(
        const kb::scene::Scene& scene, kb::scene::SceneEntity dropdown, std::uint64_t content);
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
