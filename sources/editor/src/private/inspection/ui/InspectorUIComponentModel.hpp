#pragma once

#include "engine/scene/SceneEntity.hpp"
#include "engine/ui/UIComponentPropertyCatalog.hpp"
#include "inspection/InspectorPanelState.hpp"

#include <optional>
#include <array>
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
