#include "EditorTestSupport.hpp"
#include "EditorTestSuites.hpp"

#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneEntities.hpp"
#include "engine/scene/SceneHierarchyAccess.hpp"
#include "engine/scene/SceneObjectDesc.hpp"
#include "scene/EditorHierarchyExpansionState.hpp"
#include "scene/EditorHierarchyRowBuilder.hpp"
#include "scene/EditorHierarchySearchMatcher.hpp"
#include "scene/EditorHierarchySearchState.hpp"

#include <string>
#include <vector>

namespace {

void RunSearchStateTest() {
    kb::editor::EditorHierarchySearchState state;

    kb::editor::tests::Require(!state.IsFocused(), "Hierarchy search should start unfocused");
    state.Focus(true);
    kb::editor::tests::Require(state.IsFocused(), "Hierarchy search focus flag was not stored");

    state.AppendAscii(L'C');
    state.AppendAscii(L'a');
    state.AppendAscii(L'\n');
    state.AppendAscii(0x0105);
    kb::editor::tests::Require(state.Query() == "Ca", "Hierarchy search accepted non-printable or non-ASCII input");

    state.Backspace();
    kb::editor::tests::Require(state.Query() == "C", "Hierarchy search backspace did not remove the last character");

    state.SetQuery("Camera");
    kb::editor::tests::Require(state.Query() == "Camera", "Hierarchy search SetQuery did not replace text");
    state.Clear();
    kb::editor::tests::Require(state.Query().empty(), "Hierarchy search Clear did not empty text");
}

void RunSearchMatcherTest() {
    const std::string normalized = kb::editor::EditorHierarchySearchMatcher::Normalize("Main Camera");
    kb::editor::tests::Require(normalized == "main camera", "Hierarchy search normalization should lowercase ASCII text");
    kb::editor::tests::Require(kb::editor::EditorHierarchySearchMatcher::Matches("Main Camera", "camera"), "Hierarchy search should match substrings case-insensitively");
    kb::editor::tests::Require(!kb::editor::EditorHierarchySearchMatcher::Matches("Main Camera", "light"), "Hierarchy search returned a false positive");
}

void RunExpansionStateTest() {
    kb::editor::EditorHierarchyExpansionState state;
    const kb::scene::SceneEntity entity{ 42U };

    state.SetExpanded(entity, false);
    kb::editor::tests::Require(state.CollapsedEntities().contains(entity.Id()), "Hierarchy expansion state did not mark entity collapsed");

    state.SetExpanded(entity, true);
    kb::editor::tests::Require(!state.CollapsedEntities().contains(entity.Id()), "Hierarchy expansion state did not remove expanded entity");
}

[[nodiscard]] std::vector<std::string> RowNames(const std::vector<kb::editor::EditorHierarchyRow>& rows) {
    std::vector<std::string> names;
    names.reserve(rows.size());
    for (const kb::editor::EditorHierarchyRow& row : rows) {
        names.push_back(row.name);
    }
    return names;
}

void RunRowBuilderCollapsedTreeTest() {
    kb::scene::Scene scene;
    const kb::scene::SceneObject root = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{ .name = "Root" });
    static_cast<void>(scene.Entities().CreateObject(kb::scene::SceneObjectDesc{ .name = "Child", .parent = root }));

    kb::editor::EditorHierarchyRowBuilder::CollapsedEntitySet collapsed;
    collapsed.insert(root.Entity().Id());

    const std::vector<kb::editor::EditorHierarchyRow> rows = kb::editor::EditorHierarchyRowBuilder::Build(scene, collapsed, "");
    kb::editor::tests::Require(rows.size() == 1, "Collapsed hierarchy should only expose the root row");
    kb::editor::tests::Require(rows[0].name == "Root", "Collapsed hierarchy root row has invalid name");
    kb::editor::tests::Require(rows[0].hasChildren, "Collapsed hierarchy root should report children");
    kb::editor::tests::Require(!rows[0].expanded, "Collapsed hierarchy root should not be expanded");
}

void RunRowBuilderFilteredTreeTest() {
    kb::scene::Scene scene;
    const kb::scene::SceneObject root = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{ .name = "Root" });
    const kb::scene::SceneObject cameraRig = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{ .name = "Camera Rig", .parent = root });
    static_cast<void>(scene.Entities().CreateObject(kb::scene::SceneObjectDesc{ .name = "Main Camera", .parent = cameraRig }));
    static_cast<void>(scene.Entities().CreateObject(kb::scene::SceneObjectDesc{ .name = "Key Light", .parent = root }));

    const std::vector<kb::editor::EditorHierarchyRow> rows = kb::editor::EditorHierarchyRowBuilder::Build(scene, {}, "camera");
    const std::vector<std::string> names = RowNames(rows);

    kb::editor::tests::Require(names.size() == 3, "Filtered hierarchy should include matching descendants and ancestors");
    kb::editor::tests::Require(names[0] == "Root", "Filtered hierarchy should keep root ancestor first");
    kb::editor::tests::Require(names[1] == "Camera Rig", "Filtered hierarchy should keep matching parent");
    kb::editor::tests::Require(names[2] == "Main Camera", "Filtered hierarchy should keep matching child");
    kb::editor::tests::Require(rows[0].expanded && rows[1].expanded, "Filtered hierarchy ancestors with matches should be expanded");
    kb::editor::tests::Require(!rows[2].expanded, "Filtered hierarchy leaf should not be expanded");
}

} // namespace

namespace kb::editor::tests {

void RunEditorHierarchyTests() {
    RunSearchStateTest();
    RunSearchMatcherTest();
    RunExpansionStateTest();
    RunRowBuilderCollapsedTreeTest();
    RunRowBuilderFilteredTreeTest();
}

} // namespace kb::editor::tests
