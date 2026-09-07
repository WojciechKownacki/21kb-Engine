#pragma once

#include "engine/scene/SceneEntity.hpp"
#include "engine/ui/UIComponentPropertyCatalog.hpp"
#include "inspection/InspectorPanelState.hpp"

#include <optional>
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
};

} // namespace kb::editor
