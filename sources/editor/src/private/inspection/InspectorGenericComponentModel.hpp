#pragma once

#include "inspection/InspectorPanelState.hpp"
#include "rendering/HeroIconKind.hpp"

#include "engine/scene/SceneEntity.hpp"
#include "engine/script/ScriptSceneComponentApi.hpp"
#include "engine/script/ScriptValue.hpp"

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
        std::string_view unit;
        bool isBool = false;
        bool boolValue = false;
        // A row of the section's Advanced part; Rows() lists the other rows first.
        bool advanced = false;
        int editIndex = -1;
    };
    // The values of one component, by property name, for Copy and Paste.
    using Values = std::vector<std::pair<std::string, kb::script::ScriptValue>>;
    // The entry of the component's property table behind a row's edit index.
    struct Property {
        const Section* section = nullptr;
        const kb::script::ScriptSceneComponentPropertyDesc* desc = nullptr;
    };

    InspectorGenericComponentModel() = delete;
    [[nodiscard]] static std::span<const Section> Sections() noexcept;
    [[nodiscard]] static const Section* Find(InspectorSectionId id) noexcept;
    // The rows of a section: first the ones every author uses, then the ones that wait behind the Advanced row. A
    // section that names no basic rows shows all of them as basic.
    [[nodiscard]] static std::vector<Row> Rows(kb::scene::Scene& scene, kb::scene::SceneEntity entity, const Section& section);
    [[nodiscard]] static int BasicCount(const std::vector<Row>& rows) noexcept;
    [[nodiscard]] static Property Locate(int editIndex) noexcept;
    // The asset type a property takes, so it can offer a menu of them instead of a number; empty for the rest.
    [[nodiscard]] static std::string_view AssetTypeOf(std::string_view component, std::string_view property) noexcept;
    [[nodiscard]] static std::string ValueText(kb::scene::Scene& scene, std::string_view component,
        const kb::script::ScriptSceneComponentPropertyDesc& desc, const kb::script::ScriptValue& value);
    // What the row says in plain words (the component's Inspector description), by the row's edit index.
    [[nodiscard]] static std::string_view Tooltip(int editIndex);
    // The rows whose value differs from the component as Add Component creates it, by edit index. Asset slots are
    // choices and never appear.
    [[nodiscard]] static std::vector<int> ChangedRows(const kb::scene::Scene& scene, kb::scene::SceneEntity entity, const Section& section);
    [[nodiscard]] static bool ResetRow(kb::scene::Scene& scene, kb::scene::SceneEntity entity, int editIndex);
    [[nodiscard]] static bool Capture(const kb::scene::Scene& scene, kb::scene::SceneEntity entity, const Section& section, Values& out);
    [[nodiscard]] static bool Apply(kb::scene::Scene& scene, kb::scene::SceneEntity entity, const Section& section, const Values& values);
    [[nodiscard]] static bool Click(EditorSceneContext& sceneContext, kb::scene::SceneEntity entity, int editIndex);
    [[nodiscard]] static bool ApplyText(EditorSceneContext& sceneContext, kb::scene::SceneEntity entity, int editIndex, std::string_view text);
};

} // namespace kb::editor
