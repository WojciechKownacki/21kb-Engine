#pragma once

#include "inspection/InspectorPanelState.hpp"
#include "rendering/HeroIconKind.hpp"

#include "engine/scene/SceneEntity.hpp"

#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace kb::scene { class Scene; }

namespace kb::editor {

class EditorSceneContext;

// Inspector sections drawn from a component's script property table (ScriptSceneComponentApi)
// and its Inspector labels (EngineLibraryComponentInspectorDesc): one row per field, a bool
// toggles on click, an enum steps through its names, an asset slot opens a menu of the assets it
// takes, and the rest edit as text. A component joins by adding a line to Sections().
// Rows carry InspectorPropertyId::GenericComponentField with an edit index of
// section * kFieldsPerSection + field.
class InspectorGenericComponentModel final {
public:
    static constexpr int kFieldsPerSection = 256;

    struct Section {
        std::string_view component;
        InspectorSectionId id;
        HeroIconKind icon;
        std::string_view title;
    };
    struct Row {
        std::string label;
        std::string value;
        bool isBool = false;
        bool boolValue = false;
        int editIndex = -1;
    };

    InspectorGenericComponentModel() = delete;
    [[nodiscard]] static std::span<const Section> Sections() noexcept;
    [[nodiscard]] static const Section* Find(InspectorSectionId id) noexcept;
    [[nodiscard]] static std::vector<Row> Rows(kb::scene::Scene& scene, kb::scene::SceneEntity entity, const Section& section);
    [[nodiscard]] static bool Click(EditorSceneContext& sceneContext, kb::scene::SceneEntity entity, int editIndex);
    [[nodiscard]] static bool ApplyText(EditorSceneContext& sceneContext, kb::scene::SceneEntity entity, int editIndex, std::string_view text);
};

} // namespace kb::editor
