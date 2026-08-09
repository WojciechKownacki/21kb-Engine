#include "EditorTestSupport.hpp"
#include "EditorTestSuites.hpp"

#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneAssets.hpp"
#include "engine/scene/SceneEntities.hpp"
#include "engine/scene/SceneHierarchyAccess.hpp"
#include "engine/scene/SceneComponents.hpp"
#include "engine/scene/DrawD3DeformedGeometryComponent.hpp"
#include "engine/scene/MeshRendererComponent.hpp"
#include "engine/scene/SceneObjectDesc.hpp"
#include "engine/scene/ScenePrefab.hpp"
#include "engine/scene/ScenePrefabInstance.hpp"
#include "engine/scene/ScenePrefabs.hpp"
#include "engine/scene/SceneTransforms.hpp"
#include "engine/scene/SkeletonBindingComponent.hpp"
#include "scene/EditorSceneMeshAssetActions.hpp"
#include "scene/EditorScenePrefabActions.hpp"
#include "scene/EditorHierarchyExpansionState.hpp"
#include "scene/EditorHierarchyRowBuilder.hpp"
#include "scene/EditorHierarchySearchMatcher.hpp"
#include "scene/EditorHierarchySelectionState.hpp"
#include "scene/EditorHierarchySelectionNormalizer.hpp"
#include "scene/EditorHierarchySearchState.hpp"
#include "scene/EditorSceneSelectionPivot.hpp"

#include <array>
#include <string>
#include <filesystem>
#include <optional>
#include <system_error>
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
    state.SelectAll();
    state.AppendAscii(L'L');
    kb::editor::tests::Require(state.Query() == "L", "Hierarchy search select-all should replace text on input");
    state.SetQuery("Camera");
    state.SelectAll();
    state.Backspace();
    kb::editor::tests::Require(state.Query().empty(), "Hierarchy search select-all backspace should clear text");
    state.Insert("Light");
    kb::editor::tests::Require(state.Query() == "Light", "Hierarchy search insert should accept pasted ASCII input");
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

void RunRowBuilderCreationOrderTest() {
    kb::scene::Scene scene;
    static_cast<void>(scene.Entities().CreateObject(kb::scene::SceneObjectDesc{ .name = "First" }));
    static_cast<void>(scene.Entities().CreateObject(kb::scene::SceneObjectDesc{ .name = "Second" }));
    static_cast<void>(scene.Entities().CreateObject(kb::scene::SceneObjectDesc{ .name = "Third" }));

    const std::vector<kb::editor::EditorHierarchyRow> rows = kb::editor::EditorHierarchyRowBuilder::Build(scene, {}, "");
    const std::vector<std::string> names = RowNames(rows);

    kb::editor::tests::Require(names.size() == 3, "Hierarchy row builder should expose every root object");
    kb::editor::tests::Require(names[0] == "First", "Hierarchy row builder should keep the first created root at the top");
    kb::editor::tests::Require(names[1] == "Second", "Hierarchy row builder should keep middle roots in creation order");
    kb::editor::tests::Require(names[2] == "Third", "Hierarchy row builder should put the newest root at the bottom");
}

void RunHierarchySelectionModelTest() {
    const std::vector<kb::editor::EditorHierarchyRow> rows{
        kb::editor::EditorHierarchyRow{ .entity = kb::scene::SceneEntity{ 1U }, .name = "A" },
        kb::editor::EditorHierarchyRow{ .entity = kb::scene::SceneEntity{ 2U }, .name = "B" },
        kb::editor::EditorHierarchyRow{ .entity = kb::scene::SceneEntity{ 3U }, .name = "C" },
    };
    kb::editor::EditorHierarchySelectionState selection;

    kb::editor::tests::Require(selection.SelectRow(rows, 0, false, false), "Hierarchy single selection failed");
    kb::editor::tests::Require(selection.SelectedEntities().size() == 1, "Single selection should select exactly one entity");
    kb::editor::tests::Require(selection.IsSelected(rows[0].entity), "Single selection did not select the clicked row");

    kb::editor::tests::Require(selection.SelectRow(rows, 1, true, false), "Hierarchy Ctrl selection failed");
    kb::editor::tests::Require(selection.SelectedEntities().size() == 2, "Ctrl selection should add a row");
    kb::editor::tests::Require(selection.IsSelected(rows[0].entity), "Ctrl selection lost the previous row");
    kb::editor::tests::Require(selection.IsSelected(rows[1].entity), "Ctrl selection did not add the clicked row");
    kb::editor::tests::Require(selection.Primary() == rows[1].entity, "Ctrl selection should make the clicked row primary");

    kb::editor::tests::Require(selection.SelectRow(rows, 0, true, false), "Hierarchy Ctrl toggle failed");
    kb::editor::tests::Require(selection.SelectedEntities().size() == 1, "Ctrl toggle should remove an already selected row");
    kb::editor::tests::Require(!selection.IsSelected(rows[0].entity), "Ctrl toggle left the toggled row selected");
    kb::editor::tests::Require(selection.IsSelected(rows[1].entity), "Ctrl toggle removed the wrong row");

    kb::editor::tests::Require(selection.SelectRow(rows, 0, false, false), "Hierarchy range anchor selection failed");
    kb::editor::tests::Require(selection.SelectRow(rows, 2, false, true), "Hierarchy Shift range selection failed");
    kb::editor::tests::Require(selection.SelectedEntities().size() == 3, "Shift range should select every row between anchor and clicked row");
    kb::editor::tests::Require(selection.IsSelected(rows[0].entity), "Shift range missed the anchor row");
    kb::editor::tests::Require(selection.IsSelected(rows[1].entity), "Shift range missed the middle row");
    kb::editor::tests::Require(selection.IsSelected(rows[2].entity), "Shift range missed the clicked row");
    kb::editor::tests::Require(selection.Primary() == rows[2].entity, "Shift range should make the clicked row primary");

    kb::editor::tests::Require(selection.SelectRow(rows, 1, false, false), "Hierarchy selected-row pointer down failed");
    kb::editor::tests::Require(selection.SelectedEntities().size() == 3, "Pointer down on an already multi-selected row should preserve the selection for dragging");
    kb::editor::tests::Require(selection.IsSelected(rows[0].entity), "Selected-row pointer down lost the first selected row");
    kb::editor::tests::Require(selection.IsSelected(rows[1].entity), "Selected-row pointer down lost the clicked selected row");
    kb::editor::tests::Require(selection.IsSelected(rows[2].entity), "Selected-row pointer down lost the last selected row");
    kb::editor::tests::Require(selection.Primary() == rows[1].entity, "Selected-row pointer down should make the clicked row primary");

    kb::editor::tests::Require(selection.SelectRow(rows, 0, false, false), "Hierarchy preserved selection did not update primary");
    kb::editor::tests::Require(selection.SelectedEntities().size() == 3, "Pointer down on another already selected row should keep preserving the selection");
    kb::editor::tests::Require(selection.Primary() == rows[0].entity, "Selected-row pointer down should update primary each time");

    kb::editor::tests::Require(selection.SelectRow(rows, 0, true, false), "Hierarchy Ctrl remove after preserved selection failed");
    kb::editor::tests::Require(!selection.IsSelected(rows[0].entity), "Ctrl remove should still remove a selected row after preserved pointer down");

    kb::editor::tests::Require(selection.SelectRow(rows, 0, false, false), "Hierarchy single selection after preserved selection failed");
    kb::editor::tests::Require(selection.SelectedEntities().size() == 1, "Pointer down on a non-selected row should still collapse selection");
    kb::editor::tests::Require(selection.IsSelected(rows[0].entity), "Collapsed selection should select the clicked row");

    const std::array<kb::scene::SceneEntity, 3> batch{
        kb::scene::SceneEntity{ 11U },
        kb::scene::SceneEntity{ 12U },
        kb::scene::SceneEntity{ 13U },
    };
    selection.SelectEntities(batch);
    kb::editor::tests::Require(selection.SelectedEntities().size() == batch.size(), "Batch selection should select every valid entity");
    kb::editor::tests::Require(selection.Primary() == batch.back(), "Batch selection should make the last entity primary");
    kb::editor::tests::Require(selection.IsSelected(batch.front()), "Batch selection missed the first entity");
    kb::editor::tests::Require(selection.IsSelected(batch.back()), "Batch selection missed the last entity");
}

void RunHierarchySelectionNormalizerKeepsAliveMultiSelectionTest() {
    kb::scene::Scene scene;
    const kb::scene::SceneObject first = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{ .name = "First" });
    const kb::scene::SceneObject second = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{ .name = "Second" });
    const kb::scene::SceneEntity stale{ 0xFFFFU };

    kb::editor::EditorHierarchySelectionState selection;
    const std::array<kb::scene::SceneEntity, 3> selected{ first.Entity(), stale, second.Entity() };
    selection.SelectEntities(selected);

    const std::vector<kb::editor::EditorHierarchyRow> rows = kb::editor::EditorHierarchyRowBuilder::Build(scene, {}, "");
    kb::editor::EditorHierarchySelectionNormalizer::NormalizeAfterSceneRestore(scene, selection, rows);

    kb::editor::tests::Require(selection.SelectedEntities().size() == 2U, "Selection normalizer should discard stale entities");
    kb::editor::tests::Require(selection.IsSelected(first.Entity()), "Selection normalizer lost the first alive entity");
    kb::editor::tests::Require(selection.IsSelected(second.Entity()), "Selection normalizer lost the second alive entity");
    kb::editor::tests::Require(selection.Primary() == second.Entity(), "Selection normalizer should preserve the alive primary entity");
}

void RunHierarchySelectionNormalizerSelectsFirstVisibleWhenSelectionIsDeadTest() {
    kb::scene::Scene scene;
    const kb::scene::SceneObject first = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{ .name = "First" });

    kb::editor::EditorHierarchySelectionState selection;
    selection.SelectEntity(kb::scene::SceneEntity{ 0xFFFFU });

    const std::vector<kb::editor::EditorHierarchyRow> rows = kb::editor::EditorHierarchyRowBuilder::Build(scene, {}, "");
    kb::editor::EditorHierarchySelectionNormalizer::NormalizeAfterSceneRestore(scene, selection, rows);

    kb::editor::tests::Require(selection.SelectedEntities().size() == 1U, "Selection normalizer should recover a visible selection when all selected entities are dead");
    kb::editor::tests::Require(selection.Primary() == first.Entity(), "Selection normalizer should select the first visible row as fallback");
}

void RunSceneSelectionPivotUsesTopLevelSelectionCenterTest() {
    kb::scene::Scene scene;
    const kb::scene::SceneObject parent = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{ .name = "Parent" });
    const kb::scene::SceneObject child = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{ .name = "Child", .parent = parent });
    const kb::scene::SceneObject sibling = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{ .name = "Sibling" });

    kb::scene::TransformComponent parentTransform = scene.Transforms().Get(parent);
    parentTransform.localPosition = kb::scene::Vec3{ 2.0F, 0.0F, 0.0F };
    scene.Transforms().Set(parent, parentTransform);

    kb::scene::TransformComponent childTransform = scene.Transforms().Get(child);
    childTransform.localPosition = kb::scene::Vec3{ 100.0F, 0.0F, 0.0F };
    scene.Transforms().Set(child, childTransform);

    kb::scene::TransformComponent siblingTransform = scene.Transforms().Get(sibling);
    siblingTransform.localPosition = kb::scene::Vec3{ 6.0F, 0.0F, 0.0F };
    scene.Transforms().Set(sibling, siblingTransform);

    const std::array<kb::scene::SceneEntity, 3> selected{ parent.Entity(), child.Entity(), sibling.Entity() };
    const std::optional<kb::scene::Vec3> pivot = kb::editor::EditorSceneSelectionPivot::Resolve(scene, selected, {});

    kb::editor::tests::Require(pivot.has_value(), "Selection pivot should resolve for alive selected entities");
    kb::editor::tests::Require(pivot->x == 4.0F && pivot->y == 0.0F && pivot->z == 0.0F, "Selection pivot should average top-level selected transforms only");
}

void RunRowBuilderMarksOnlyPrefabRootsTest() {
    kb::scene::Scene scene;
    kb::scene::ScenePrefab prefab;
    const std::uint32_t rootNode = prefab.AddNode(kb::scene::ScenePrefabNodeDesc{ .name = "Prefab Root" });
    const std::uint32_t childNode = prefab.AddNode(kb::scene::ScenePrefabNodeDesc{
        .name = "Prefab Child",
        .parentNode = rootNode,
    });

    const kb::scene::ScenePrefabHandle handle = scene.Prefabs().Register("HierarchyPrefab", std::move(prefab));
    const kb::scene::ScenePrefabInstance instance = scene.Prefabs().Instantiate(handle);
    kb::editor::tests::Require(!instance.Empty(), "Hierarchy prefab test setup failed to instantiate prefab");
    kb::editor::tests::Require(scene.Prefabs().RootInstance(instance.ObjectAt(rootNode)).IsValid(), "Prefab root should own the root prefab instance handle");
    kb::editor::tests::Require(!scene.Prefabs().RootInstance(instance.ObjectAt(childNode)).IsValid(), "Prefab child must not own a root prefab instance handle");
    std::uint32_t childContainingNode = 99;
    kb::editor::tests::Require(scene.Prefabs().ContainingInstance(instance.ObjectAt(childNode), childContainingNode).IsValid(), "Prefab child should remain tracked as a node inside the parent prefab instance");
    kb::editor::tests::Require(childContainingNode == childNode, "Prefab child should map to its prefab node index, not to a separate prefab root");

    const std::vector<kb::editor::EditorHierarchyRow> rows = kb::editor::EditorHierarchyRowBuilder::Build(scene, {}, "");
    kb::editor::tests::Require(rows.size() == 2, "Hierarchy should expose prefab root and child rows");
    kb::editor::tests::Require(rows[0].prefabRoot, "Hierarchy prefab root row should render with prefab styling");
    kb::editor::tests::Require(!rows[1].prefabRoot, "Hierarchy prefab child row should not render as a prefab root");
}

void RunDroppedPrefabAssetBuildsPrefabHierarchyRowsTest() {
    const std::filesystem::path projectRoot = std::filesystem::temp_directory_path() / "21kb_editor_hierarchy_dropped_prefab_project";
    const std::filesystem::path prefabPath = projectRoot / "Assets" / "Prefabs" / "DroppedPrefab.kbprefab";
    std::error_code removeError;
    std::filesystem::remove_all(projectRoot, removeError);
    std::filesystem::create_directories(prefabPath.parent_path(), removeError);
    kb::editor::tests::Require(!removeError, "Dropped prefab test setup failed to create asset folder");

    kb::scene::Scene source;
    kb::scene::ScenePrefab prefab;
    static_cast<void>(prefab.AddNode(kb::scene::ScenePrefabNodeDesc{ .name = "Dropped Prefab Root" }));
    const kb::scene::ScenePrefabHandle handle = source.Prefabs().Register("DroppedPrefab", std::move(prefab));
    kb::editor::tests::Require(handle.IsValid(), "Dropped prefab test setup failed to register prefab");
    kb::editor::tests::Require(source.Prefabs().Save(handle, prefabPath), "Dropped prefab test setup failed to save prefab asset");

    kb::scene::Scene scene;
    kb::editor::tests::Require(scene.Assets().MountProject(projectRoot), "Dropped prefab test setup failed to mount project assets");
    const std::optional<kb::scene::SceneEntity> entity = kb::editor::EditorScenePrefabActions::InstantiateAsset(scene, {}, "/Game/Prefabs/DroppedPrefab.kbprefab", {});
    kb::editor::tests::Require(entity.has_value() && entity->IsValid(), "Dropping a prefab asset into hierarchy should create a scene entity");

    std::uint32_t nodeIndex = 99;
    kb::editor::tests::Require(scene.Prefabs().ContainingInstance(*entity, nodeIndex).IsValid(), "Dropped prefab asset should create a tracked prefab instance");
    kb::editor::tests::Require(nodeIndex == 0, "Dropped prefab root should map to prefab node 0");

    const std::vector<kb::editor::EditorHierarchyRow> rows = kb::editor::EditorHierarchyRowBuilder::Build(scene, {}, "");
    kb::editor::tests::Require(rows.size() == 1, "Dropped prefab should add one hierarchy row");
    kb::editor::tests::Require(rows[0].prefabRoot, "Dropped prefab hierarchy row should render as a prefab root");

    std::filesystem::remove_all(projectRoot, removeError);
}

void RunMeshAssetActionCreatesRenderableSceneEntityTest() {
    kb::scene::Scene scene;
    constexpr kb::assets::AssetId meshAssetId{ 0x2100ULL };

    const kb::scene::SceneEntity entity = kb::editor::EditorSceneMeshAssetActions::CreateMeshEntity(scene, meshAssetId, "Imported Cube");
    kb::editor::tests::Require(entity.IsValid(), "Mesh asset action did not create a scene entity");
    kb::editor::tests::Require(scene.Entities().IsAlive(entity), "Mesh asset action returned an entity that is not alive");
    kb::editor::tests::Require(scene.Entities().Name(entity) == "Imported Cube", "Mesh asset action did not preserve the asset name");

    const kb::scene::MeshRendererComponent* renderer = scene.Components().MeshRenderers().TryGet(entity);
    kb::editor::tests::Require(renderer != nullptr, "Mesh asset action did not attach a mesh renderer component");
    kb::editor::tests::Require(renderer->meshAssetId == meshAssetId.value, "Mesh renderer component did not reference the imported mesh asset");
}

void RunSkeletalMeshAssetActionCreatesDeformedSceneEntityTest() {
    kb::scene::Scene scene;
    constexpr kb::assets::AssetId meshAssetId{ 0x2101ULL };
    constexpr kb::assets::AssetId skeletonAssetId{ 0x5101ULL };
    constexpr std::uint64_t compatibilitySignature = 0xC001ULL;

    const kb::scene::SceneEntity entity = kb::editor::EditorSceneMeshAssetActions::CreateSkeletalMeshEntity(
        scene, meshAssetId, skeletonAssetId, compatibilitySignature, "Imported Character");
    kb::editor::tests::Require(entity.IsValid() && scene.Entities().IsAlive(entity), "Skeletal Mesh asset action did not create a scene entity");
    const kb::scene::MeshRendererComponent* renderer = scene.Components().MeshRenderers().TryGet(entity);
    const kb::scene::DrawD3DeformedGeometryComponent* geometry = scene.Components().DeformedGeometries().TryGet(entity);
    const kb::scene::SkeletonBindingComponent* binding = scene.Components().SkeletonBindings().TryGet(entity);
    kb::editor::tests::Require(renderer != nullptr && renderer->meshAssetId == meshAssetId.value, "Skeletal Mesh entity did not attach its renderer synchronization component");
    kb::editor::tests::Require(geometry != nullptr && geometry->enabled && geometry->skeletalMeshAssetId == meshAssetId.value, "Skeletal Mesh entity did not attach enabled deformed geometry");
    kb::editor::tests::Require(binding != nullptr && binding->enabled && binding->skeletonAssetId == skeletonAssetId.value &&
            binding->skeletonCompatibilitySignature == compatibilitySignature,
        "Skeletal Mesh entity did not attach its compatible skeleton binding");
}

} // namespace

namespace kb::editor::tests {

void RunEditorHierarchyTests() {
    RunSearchStateTest();
    RunSearchMatcherTest();
    RunExpansionStateTest();
    RunRowBuilderCollapsedTreeTest();
    RunRowBuilderFilteredTreeTest();
    RunRowBuilderCreationOrderTest();
    RunHierarchySelectionModelTest();
    RunHierarchySelectionNormalizerKeepsAliveMultiSelectionTest();
    RunHierarchySelectionNormalizerSelectsFirstVisibleWhenSelectionIsDeadTest();
    RunSceneSelectionPivotUsesTopLevelSelectionCenterTest();
    RunRowBuilderMarksOnlyPrefabRootsTest();
    RunDroppedPrefabAssetBuildsPrefabHierarchyRowsTest();
    RunMeshAssetActionCreatesRenderableSceneEntityTest();
    RunSkeletalMeshAssetActionCreatesDeformedSceneEntityTest();
}

} // namespace kb::editor::tests
