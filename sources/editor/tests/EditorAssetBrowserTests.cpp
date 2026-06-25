#include "EditorTestSupport.hpp"
#include "EditorTestSuites.hpp"

#include "assets/EditorAssetBrowserHitTester.hpp"
#include "assets/EditorAssetBrowserLayout.hpp"
#include "assets/EditorAssetBrowserState.hpp"
#include "app/EditorAssetBrowserDoubleClickHandler.hpp"
#include "engine/assets/AssetManager.hpp"
#if defined(_WIN32)
#include "rendering/ProjectFilesAssetIconResolver.hpp"
#endif

#include <algorithm>
#include <filesystem>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

[[nodiscard]] kb::assets::AssetMetadata Metadata(std::string name, std::string type, std::filesystem::path path) {
    return kb::assets::AssetMetadata{
        .type = std::move(type),
        .name = std::move(name),
        .virtualPath = std::move(path),
        .runtimeLoadable = true,
    };
}

void RunFolderTreeTest() {
    kb::assets::AssetManager manager;
    static_cast<void>(manager.RegisterAsset(Metadata("Player", "ScenePrefab", "/Game/Prefabs/Player.kbprefab")));
    static_cast<void>(manager.RegisterAsset(Metadata("Stone", "ScenePrefab", "/Game/Environment/Rocks/Stone.kbprefab")));

    kb::editor::EditorAssetBrowserState state;
    std::vector<kb::editor::EditorAssetFolderRow> folders = state.FolderRows(manager);

    kb::editor::tests::Require(folders.size() == 4, "Asset browser should expose root and nested virtual folders");
    kb::editor::tests::Require(folders[0].virtualPath == "/Game", "Asset browser folder tree should start at /Game");
    kb::editor::tests::Require(folders[0].hasChildren && folders[0].expanded, "Asset browser root folder should render as an expanded tree node");
    kb::editor::tests::Require(folders[1].virtualPath == "/Game/Environment", "Asset browser missed first nested folder");
    kb::editor::tests::Require(folders[1].hasChildren && folders[1].expanded, "Asset browser nested folder should expose expanded tree state");
    kb::editor::tests::Require(folders[2].virtualPath == "/Game/Environment/Rocks", "Asset browser missed second nested folder");
    kb::editor::tests::Require(!folders[2].hasChildren && !folders[2].expanded, "Asset browser leaf folder should not expose an expansion triangle");
    kb::editor::tests::Require(folders[3].virtualPath == "/Game/Prefabs", "Asset browser should sort virtual folders");

    kb::editor::tests::Require(state.ToggleFolderExpanded("/Game/Environment", manager), "Asset browser should collapse folders with children");
    folders = state.FolderRows(manager);
    kb::editor::tests::Require(folders.size() == 3, "Asset browser should hide child folder rows below a collapsed folder");
    kb::editor::tests::Require(std::ranges::none_of(folders, [](const kb::editor::EditorAssetFolderRow& row) {
        return row.virtualPath == "/Game/Environment/Rocks";
    }), "Asset browser collapsed folder should hide its descendants");

    kb::editor::tests::Require(state.SelectFolder("/Game/Environment/Rocks", manager), "Asset browser should still navigate to a folder hidden by collapsed ancestors");
    folders = state.FolderRows(manager);
    kb::editor::tests::Require(folders.size() == 4, "Asset browser navigation should expand ancestors of the selected folder");
}

void RunFilteringTest() {
    kb::assets::AssetManager manager;
    static_cast<void>(manager.RegisterAsset(Metadata("Player", "ScenePrefab", "/Game/Prefabs/Player.kbprefab")));
    static_cast<void>(manager.RegisterAsset(Metadata("Readme", "Text", "/Game/Docs/Readme.txt")));

    kb::editor::EditorAssetBrowserState state;
    static_cast<void>(state.SelectFolder("/Game/Prefabs", manager));
    std::vector<kb::editor::EditorAssetItemRow> assets = state.AssetRows(manager);
    kb::editor::tests::Require(assets.size() == 1 && assets[0].metadata.name == "Player", "Asset browser should filter by selected folder");

    state.SetSearchQuery("player");
    assets = state.AssetRows(manager);
    kb::editor::tests::Require(assets.size() == 1, "Asset browser search should match asset names case-insensitively");

    state.SetSearchQuery("readme");
    assets = state.AssetRows(manager);
    kb::editor::tests::Require(assets.empty(), "Asset browser search should still honor selected folder");

    state.SetRecursive(true);
    static_cast<void>(state.SelectFolder("/Game", manager));
    state.SetSearchQuery("readme");
    assets = state.AssetRows(manager);
    kb::editor::tests::Require(assets.size() == 1 && assets[0].metadata.type == "Text", "Asset browser recursive search should include descendants");
}

void RunMaterialSearchAndFilterTest() {
    kb::assets::AssetManager manager;
    static_cast<void>(manager.RegisterAsset(Metadata("BrassCoat", "RenderMaterial", "/Game/Props/BrassCoat.kbmat")));
    static_cast<void>(manager.RegisterAsset(Metadata("HeroCoatOverride", "RenderMaterialInstance", "/Game/Characters/HeroCoatOverride.kbmatinst")));
    static_cast<void>(manager.RegisterAsset(Metadata("HeroMesh", "RenderMesh", "/Game/Characters/HeroMesh.gltf")));
    static_cast<void>(manager.RegisterAsset(Metadata("BrushTexture", "RenderTexture", "/Game/Props/BrushTexture.ktx")));

    kb::editor::EditorAssetBrowserState state;
    static_cast<void>(state.SelectFolder("/Game", manager));
    state.SetRecursive(true);
    state.SetSearchQuery("materialy");
    std::vector<kb::editor::EditorAssetItemRow> assets = state.AssetRows(manager);
    kb::editor::tests::Require(assets.size() == 2, "Asset browser material search should find every material asset type");
    kb::editor::tests::Require(std::ranges::any_of(assets, [](const kb::editor::EditorAssetItemRow& row) { return row.metadata.type == "RenderMaterial"; }), "Asset browser material search missed RenderMaterial");
    kb::editor::tests::Require(std::ranges::any_of(assets, [](const kb::editor::EditorAssetItemRow& row) { return row.metadata.type == "RenderMaterialInstance"; }), "Asset browser material search missed RenderMaterialInstance");
    kb::editor::tests::Require(std::ranges::none_of(assets, [](const kb::editor::EditorAssetItemRow& row) { return row.metadata.type == "RenderMesh" || row.metadata.type == "RenderTexture"; }), "Asset browser material search should not include non-material assets");

    const std::vector<std::string> types = state.AssetTypes(manager);
    kb::editor::tests::Require(std::ranges::find(types, "Materials") != types.end(), "Asset browser type filters should expose a combined Materials filter");
    for (std::size_t index = 0; index <= types.size() && state.TypeFilter() != "Materials"; ++index) {
        state.CycleTypeFilter(manager);
    }
    kb::editor::tests::Require(state.TypeFilter() == "Materials", "Asset browser type cycling should reach the combined Materials filter");
    state.ClearSearch();
    assets = state.AssetRows(manager);
    kb::editor::tests::Require(assets.size() == 2, "Asset browser Materials filter should include both material asset types");
    kb::editor::tests::Require(std::ranges::all_of(assets, [](const kb::editor::EditorAssetItemRow& row) {
        return row.metadata.type == "RenderMaterial" || row.metadata.type == "RenderMaterialInstance";
    }), "Asset browser Materials filter should exclude non-material assets");
}

void RunSelectionAndTypeCycleTest() {
    kb::assets::AssetManager manager;
    static_cast<void>(manager.RegisterAsset(Metadata("Player", "ScenePrefab", "/Game/Prefabs/Player.kbprefab")));
    static_cast<void>(manager.RegisterAsset(Metadata("Readme", "Text", "/Game/Docs/Readme.txt")));

    kb::editor::EditorAssetBrowserState state;
    state.SetRecursive(true);
    const std::vector<kb::editor::EditorAssetItemRow> assets = state.AssetRows(manager);
    kb::editor::tests::Require(assets.size() == 2, "Asset browser recursive mode should show /Game descendants");

    const kb::assets::AssetId selected = manager.Registry().FindByPath("/Game/Prefabs/Player.kbprefab")->id;
    kb::editor::tests::Require(state.SelectAsset(selected, manager), "Asset browser failed to select registered asset");
    kb::editor::tests::Require(state.SelectedFolder() == "/Game/Prefabs", "Asset browser selection should follow asset parent folder");
    kb::editor::tests::Require(state.SelectionKind() == kb::editor::EditorAssetBrowserSelectionKind::Asset, "Asset browser asset selection should be inspector-visible");

    static_cast<void>(state.SelectFolder("/Game", manager));
    kb::editor::tests::Require(state.InspectorAsset() == selected, "Asset browser folder navigation should keep the last inspected asset");
    kb::editor::tests::Require(state.SelectContentFolder("/Game/Docs", manager), "Asset browser failed to select a child folder without navigating");
    kb::editor::tests::Require(state.SelectedFolder() == "/Game", "Asset browser child folder selection should not change the open folder");
    kb::editor::tests::Require(state.SelectionKind() == kb::editor::EditorAssetBrowserSelectionKind::Folder, "Asset browser folder selection should be inspector-visible");
    kb::editor::tests::Require(state.InspectorAsset() == selected, "Asset browser folder selection should not clear the Inspector asset");
    const std::vector<kb::editor::EditorAssetFolderRow> childFolders = state.ChildFolderRows(manager);
    const auto docs = std::ranges::find_if(childFolders, [](const kb::editor::EditorAssetFolderRow& row) {
        return row.virtualPath == "/Game/Docs";
    });
    kb::editor::tests::Require(docs != childFolders.end() && docs->selected, "Asset browser child folder row should render selected");
    kb::editor::tests::Require(state.BeginRenameSelection(manager), "Asset browser should rename the selected child folder");
    kb::editor::tests::Require(state.TextEditMode() == kb::editor::EditorAssetTextEditMode::RenameFolder, "Asset browser child folder rename should enter folder rename mode");
    kb::editor::tests::Require(state.TextEditTargetFolder() == "/Game/Docs", "Asset browser child folder rename should target the selected folder");
    state.CancelTextEdit();
    state.ClearSelection();
    kb::editor::tests::Require(state.SelectionKind() == kb::editor::EditorAssetBrowserSelectionKind::None, "Asset browser clear selection should clear inspector-visible selection");
    kb::editor::tests::Require(!state.InspectorAsset().IsValid(), "Asset browser clear selection should clear the Inspector asset");

    state.CycleTypeFilter(manager);
    kb::editor::tests::Require(!state.TypeFilter().empty(), "Asset browser type cycling should activate first type filter");
}

void RunMultiSelectionStateTest() {
    kb::assets::AssetManager manager;
    static_cast<void>(manager.RegisterAsset(Metadata("AAsset", "ScenePrefab", "/Game/A/AAsset.kbprefab")));
    static_cast<void>(manager.RegisterAsset(Metadata("BAsset", "ScenePrefab", "/Game/B/BAsset.kbprefab")));
    static_cast<void>(manager.RegisterAsset(Metadata("CAsset", "ScenePrefab", "/Game/C/CAsset.kbprefab")));
    static_cast<void>(manager.RegisterAsset(Metadata("RootAsset", "ScenePrefab", "/Game/RootAsset.kbprefab")));

    kb::editor::EditorAssetBrowserState state;
    static_cast<void>(state.SelectFolder("/Game", manager));

    kb::editor::tests::Require(state.SelectContentFolderAt(0, manager, false, false), "Asset browser should select the first visible content folder");
    kb::editor::tests::Require(state.SelectContentFolderAt(2, manager, true, false), "Asset browser Ctrl selection should add a visible content folder");
    std::vector<kb::editor::EditorAssetFolderRow> folders = state.ChildFolderRows(manager);
    kb::editor::tests::Require(folders.size() == 3, "Asset browser multi-selection test expected three visible folders");
    kb::editor::tests::Require(folders[0].selected && !folders[1].selected && folders[2].selected, "Asset browser Ctrl folder selection did not preserve existing selection");

    kb::editor::tests::Require(state.SelectContentFolderAt(2, manager, true, false), "Asset browser Ctrl selection should toggle an already selected folder");
    folders = state.ChildFolderRows(manager);
    kb::editor::tests::Require(folders[0].selected && !folders[1].selected && !folders[2].selected, "Asset browser Ctrl folder toggle did not remove the clicked folder");

    kb::editor::tests::Require(state.SelectContentFolderAt(0, manager, false, false), "Asset browser should reset folder range anchor");
    kb::editor::tests::Require(state.SelectContentFolderAt(2, manager, false, true), "Asset browser Shift selection should select a folder range");
    folders = state.ChildFolderRows(manager);
    kb::editor::tests::Require(folders[0].selected && folders[1].selected && folders[2].selected, "Asset browser Shift folder range missed visible folders");
    kb::editor::tests::Require(state.SelectedContentFolder() == folders[2].virtualPath, "Asset browser forward Shift range should make clicked folder primary");

    kb::editor::tests::Require(state.SelectContentFolderAt(2, manager, false, false), "Asset browser should reset backward folder range anchor");
    kb::editor::tests::Require(state.SelectContentFolderAt(0, manager, false, true), "Asset browser backward Shift selection should select a folder range");
    folders = state.ChildFolderRows(manager);
    kb::editor::tests::Require(folders[0].selected && folders[1].selected && folders[2].selected, "Asset browser backward Shift folder range missed visible folders");
    kb::editor::tests::Require(state.SelectedContentFolder() == folders[0].virtualPath, "Asset browser backward Shift range should make clicked folder primary");

    kb::editor::tests::Require(state.SelectAllContent(manager), "Asset browser Ctrl+A should select visible content");
    folders = state.ChildFolderRows(manager);
    const std::vector<kb::editor::EditorAssetItemRow> assets = state.AssetRows(manager);
    kb::editor::tests::Require(std::ranges::all_of(folders, [](const kb::editor::EditorAssetFolderRow& folder) {
        return folder.selected;
    }), "Asset browser Ctrl+A did not select every visible folder");
    kb::editor::tests::Require(std::ranges::all_of(assets, [](const kb::editor::EditorAssetItemRow& asset) {
        return asset.selected;
    }), "Asset browser Ctrl+A did not select every visible asset");

    const std::vector<kb::editor::EditorAssetSelectionSummaryRow> allDeleteRows = state.DeleteTargetRows(manager);
    kb::editor::tests::Require(allDeleteRows.size() == folders.size() + assets.size(), "Asset browser delete confirmation should list every selected content item");
    kb::editor::tests::Require(std::ranges::all_of(allDeleteRows, [](const kb::editor::EditorAssetSelectionSummaryRow& row) {
        return row.checked && !row.key.empty();
    }), "Asset browser delete confirmation rows should start checked and expose stable keys");
    kb::editor::tests::Require(std::ranges::any_of(allDeleteRows, [](const kb::editor::EditorAssetSelectionSummaryRow& row) {
        return row.name == "A" && row.objectType == "Folder";
    }), "Asset browser delete confirmation should include selected folders");
    kb::editor::tests::Require(std::ranges::any_of(allDeleteRows, [](const kb::editor::EditorAssetSelectionSummaryRow& row) {
        return row.name == "RootAsset" && row.objectType == "ScenePrefab";
    }), "Asset browser delete confirmation should include selected assets");
    const kb::assets::AssetMetadata* rootAsset = manager.Registry().FindByPath("/Game/RootAsset.kbprefab");
    kb::editor::tests::Require(rootAsset != nullptr, "Asset browser delete key test expected the root asset");
    const auto rootAssetDeleteRow = std::ranges::find_if(allDeleteRows, [](const kb::editor::EditorAssetSelectionSummaryRow& row) {
        return row.name == "RootAsset";
    });
    kb::editor::tests::Require(rootAssetDeleteRow != allDeleteRows.end(), "Asset browser delete key test expected a root asset row");
    kb::assets::AssetId parsedDeleteId{};
    kb::editor::tests::Require(
        rootAssetDeleteRow->key.rfind("Asset:", 0) == 0
            && kb::assets::TryParseAssetId(std::string_view(rootAssetDeleteRow->key).substr(6), parsedDeleteId)
            && parsedDeleteId == rootAsset->id,
        "Asset browser asset delete row key should round-trip through AssetId parsing");
    kb::editor::tests::Require(state.ToggleDeleteTargetChecked(allDeleteRows.front().key), "Asset browser delete confirmation should toggle a row checkbox");
    const std::vector<kb::editor::EditorAssetSelectionSummaryRow> checkedDeleteRows = state.CheckedDeleteTargetRows(manager);
    kb::editor::tests::Require(checkedDeleteRows.size() + 1 == allDeleteRows.size(), "Asset browser checked delete targets should exclude unchecked rows");

    state.ClearSelection();
    kb::editor::tests::Require(state.SelectContentFolder("/Game/B", manager), "Asset browser should select a single delete target folder");
    const std::vector<kb::editor::EditorAssetSelectionSummaryRow> singleDeleteRows = state.DeleteTargetRows(manager);
    kb::editor::tests::Require(singleDeleteRows.size() == 1, "Asset browser delete confirmation should list one row for one selected object");
    kb::editor::tests::Require(singleDeleteRows[0].id == "/Game/B" && singleDeleteRows[0].name == "B", "Asset browser single delete row should describe the selected folder");
}

void RunTextEditStateTest() {
    kb::assets::AssetManager manager;
    static_cast<void>(manager.RegisterAsset(Metadata("Player", "ScenePrefab", "/Game/Prefabs/Player.kbprefab")));

    kb::editor::EditorAssetBrowserState state;
    state.BeginNewFolder();
    kb::editor::tests::Require(state.TextEditMode() == kb::editor::EditorAssetTextEditMode::NewFolder, "Asset browser did not enter new-folder edit mode");
    state.SetTextEditValue("Gameplay");
    state.AppendTextEdit(L'_');
    state.AppendTextEdit(L'A');
    kb::editor::tests::Require(state.TextEditValue() == "Gameplay_A", "Asset browser text edit did not accept ASCII input");
    state.BackspaceTextEdit();
    kb::editor::tests::Require(state.TextEditValue() == "Gameplay_", "Asset browser text edit backspace failed");
    state.SelectAllTextEdit();
    state.AppendTextEdit(L'Q');
    kb::editor::tests::Require(state.TextEditValue() == "Q", "Asset browser text edit select-all should replace text on input");
    state.SelectAllTextEdit();
    state.BackspaceTextEdit();
    kb::editor::tests::Require(state.TextEditValue().empty(), "Asset browser text edit select-all backspace should clear text");
    state.InsertTextEdit("Gameplay");
    kb::editor::tests::Require(state.TextEditValue() == "Gameplay", "Asset browser text edit insert should accept pasted ASCII input");
    state.CancelTextEdit();
    kb::editor::tests::Require(!state.IsTextEditing(), "Asset browser text edit cancel did not clear edit mode");

    const kb::assets::AssetId selected = manager.Registry().FindByPath("/Game/Prefabs/Player.kbprefab")->id;
    static_cast<void>(state.SelectAsset(selected, manager));
    kb::editor::tests::Require(state.BeginRenameSelection(manager), "Asset browser did not enter asset rename mode");
    kb::editor::tests::Require(state.TextEditMode() == kb::editor::EditorAssetTextEditMode::RenameAsset, "Asset browser rename mode did not target selected asset");
}

void RunSearchTextShortcutStateTest() {
    kb::editor::EditorAssetBrowserState state;
    state.FocusSearch(true);
    state.SetSearchQuery("Camera");
    state.SelectAllSearch();
    state.AppendSearchText(L'L');
    kb::editor::tests::Require(state.SearchQuery() == "L", "Asset browser search select-all should replace text on input");
    state.SetSearchQuery("Camera");
    state.SelectAllSearch();
    state.BackspaceSearch();
    kb::editor::tests::Require(state.SearchQuery().empty(), "Asset browser search select-all backspace should clear text");
    state.InsertSearchText("Light");
    kb::editor::tests::Require(state.SearchQuery() == "Light", "Asset browser search insert should accept pasted ASCII input");
}

#if defined(_WIN32)
void RunProjectFilesEdgeToEdgeLayoutTest() {
    const RECT content{ 0, 0, 960, 260 };
    const kb::editor::EditorAssetBrowserLayoutRects layout = kb::editor::EditorAssetBrowserLayout::Build(content);

    kb::editor::tests::Require(layout.frame.left == content.left && layout.frame.top == content.top, "Asset browser frame should start at the panel content edge");
    kb::editor::tests::Require(layout.frame.right == content.right && layout.frame.bottom == content.bottom, "Asset browser frame should end at the panel content edge");
    kb::editor::tests::Require(layout.tree.left == layout.frame.left && layout.tree.top == layout.frame.top, "Asset browser tree should use the freed toolbar space");
    kb::editor::tests::Require(layout.toolbar.left == layout.tree.right, "Asset browser toolbar should start at the asset view column");
    kb::editor::tests::Require(layout.assetView.left == layout.tree.right, "Asset browser asset view should share the tree separator");
    kb::editor::tests::Require(layout.assetView.top == layout.toolbar.bottom, "Asset browser asset view should sit directly below the toolbar");
    kb::editor::tests::Require(layout.tree.bottom == layout.bottomBar.top && layout.assetView.bottom == layout.bottomBar.top, "Asset browser body should touch the bottom bar");
    kb::editor::tests::Require(layout.importButton.right <= layout.filtersButton.left, "Asset browser Import button should sit before Filters");

    const kb::editor::EditorAssetBrowserLayoutRects resized = kb::editor::EditorAssetBrowserLayout::Build(content, 360);
    kb::editor::tests::Require(resized.tree.right == content.left + 360, "Asset browser tree should support explicit resize width");
    kb::editor::tests::Require(resized.toolbar.left == resized.tree.right && resized.assetView.left == resized.tree.right, "Asset browser resized separator should drive the right column");
}

void RunTileHitTestUsesExactGridGeometryTest() {
    kb::assets::AssetManager manager;
    static_cast<void>(manager.RegisterAsset(Metadata("Directional_Light", "ScenePrefab", "/Game/Prefabs/Directional_Light.kbprefab")));
    static_cast<void>(manager.RegisterAsset(Metadata("Directional_Light_1", "ScenePrefab", "/Game/Prefabs/Directional_Light_1.kbprefab")));
    static_cast<void>(manager.RegisterAsset(Metadata("Main_Camera", "ScenePrefab", "/Game/Prefabs/Main_Camera.kbprefab")));

    kb::editor::EditorAssetBrowserState state;
    static_cast<void>(state.SelectFolder("/Game/Prefabs", manager));
    const RECT content{ 0, 0, 760, 260 };
    const kb::editor::EditorAssetBrowserLayoutRects layout = kb::editor::EditorAssetBrowserLayout::Build(content);
    const RECT middleTile = kb::editor::EditorAssetBrowserLayout::AssetTileRect(layout, 1, state.ThumbnailScale());
    const int x = (middleTile.left + middleTile.right) / 2;
    const int y = (middleTile.top + middleTile.bottom) / 2;

    const kb::editor::EditorAssetBrowserHit hit = kb::editor::EditorAssetBrowserHitTester::HitTest(content, x, y, state, manager);
    kb::editor::tests::Require(hit.kind == kb::editor::EditorAssetBrowserHitKind::Asset, "Asset browser middle tile should hit an asset");
    kb::editor::tests::Require(hit.index == 1U, "Asset browser middle tile hit should preserve sorted asset index");
    kb::editor::tests::Require(kb::editor::EditorAssetBrowserHitTester::PrefabAssetAt(content, x, y, state, manager).has_value(), "Asset browser middle prefab tile should be draggable");
    kb::editor::tests::Require(kb::editor::EditorAssetBrowserHitTester::AssetIdAt(content, x, y, state, manager).has_value(), "Asset browser middle prefab tile should expose its asset id for Project Files drops");
    kb::editor::tests::Require(kb::editor::EditorAssetBrowserLayout::TileHeight(state.ThumbnailScale()) > kb::editor::EditorAssetBrowserLayout::TileWidth(state.ThumbnailScale()), "Asset browser tiles should use a vertical layout");
}

void RunTreeSplitterHitTestTest() {
    kb::assets::AssetManager manager;
    kb::editor::EditorAssetBrowserState state;
    state.SetTreeWidth(360);
    const RECT content{ 0, 0, 960, 260 };
    const kb::editor::EditorAssetBrowserLayoutRects layout = kb::editor::EditorAssetBrowserLayout::Build(content, state.TreeWidth());

    const kb::editor::EditorAssetBrowserHit hit = kb::editor::EditorAssetBrowserHitTester::HitTest(content, layout.tree.right, layout.tree.top + 16, state, manager);
    kb::editor::tests::Require(hit.kind == kb::editor::EditorAssetBrowserHitKind::TreeSplitter, "Asset browser tree separator should be directly resizable");
}

void RunTreeDisclosureHitTestTest() {
    kb::assets::AssetManager manager;
    static_cast<void>(manager.RegisterAsset(Metadata("Stone", "ScenePrefab", "/Game/Environment/Rocks/Stone.kbprefab")));

    kb::editor::EditorAssetBrowserState state;
    const RECT content{ 0, 0, 760, 260 };
    const kb::editor::EditorAssetBrowserLayoutRects layout = kb::editor::EditorAssetBrowserLayout::Build(content);
    const std::vector<kb::editor::EditorAssetFolderRow> folders = state.FolderRows(manager);
    const auto environment = std::ranges::find_if(folders, [](const kb::editor::EditorAssetFolderRow& row) {
        return row.virtualPath == "/Game/Environment";
    });
    kb::editor::tests::Require(environment != folders.end(), "Asset browser tree disclosure test could not find nested folder");
    const int rowIndex = static_cast<int>(std::distance(folders.begin(), environment));
    const RECT row = kb::editor::EditorAssetBrowserLayout::FolderRowRect(layout, rowIndex);
    const int x = row.left + environment->depth * 14 + 6;
    const int y = (row.top + row.bottom) / 2;

    const kb::editor::EditorAssetBrowserHit hit = kb::editor::EditorAssetBrowserHitTester::HitTest(content, x, y, state, manager);
    kb::editor::tests::Require(hit.kind == kb::editor::EditorAssetBrowserHitKind::FolderDisclosure, "Asset browser tree triangle should be hit-testable separately from the row");
    kb::editor::tests::Require(!kb::editor::EditorAssetBrowserHitTester::FolderAt(content, x, y, state, manager).has_value(), "Asset browser tree triangle should not start a folder drag");
}

void RunContentFolderHitTestExposesFolderDragSourceTest() {
    kb::assets::AssetManager manager;
    static_cast<void>(manager.RegisterAsset(Metadata("NestedPrefab", "ScenePrefab", "/Game/Prefabs/Nested/NestedPrefab.kbprefab")));

    kb::editor::EditorAssetBrowserState state;
    static_cast<void>(state.SelectFolder("/Game/Prefabs", manager));
    const RECT content{ 0, 0, 760, 260 };
    const kb::editor::EditorAssetBrowserLayoutRects layout = kb::editor::EditorAssetBrowserLayout::Build(content);
    const RECT tile = kb::editor::EditorAssetBrowserLayout::AssetTileRect(layout, 0, state.ThumbnailScale());
    const int x = (tile.left + tile.right) / 2;
    const int y = (tile.top + tile.bottom) / 2;

    const kb::editor::EditorAssetBrowserHit hit = kb::editor::EditorAssetBrowserHitTester::HitTest(content, x, y, state, manager);
    kb::editor::tests::Require(hit.kind == kb::editor::EditorAssetBrowserHitKind::ContentFolder, "Asset browser child folder tile should hit as a folder");
    const std::optional<std::filesystem::path> folder = kb::editor::EditorAssetBrowserHitTester::FolderAt(content, x, y, state, manager);
    kb::editor::tests::Require(folder.has_value() && *folder == "/Game/Prefabs/Nested", "Asset browser child folder tile should expose a draggable folder path");
}

void RunLegacyPrefabExtensionStillDraggableTest() {
    kb::assets::AssetManager manager;
    static_cast<void>(manager.RegisterAsset(Metadata("LegacyPrefab", "LegacyPrefab", "/Game/Prefabs/LegacyPrefab.kbprefab")));

    kb::editor::EditorAssetBrowserState state;
    static_cast<void>(state.SelectFolder("/Game/Prefabs", manager));
    const RECT content{ 0, 0, 760, 260 };
    const kb::editor::EditorAssetBrowserLayoutRects layout = kb::editor::EditorAssetBrowserLayout::Build(content);
    const RECT tile = kb::editor::EditorAssetBrowserLayout::AssetTileRect(layout, 0, state.ThumbnailScale());
    const int x = (tile.left + tile.right) / 2;
    const int y = (tile.top + tile.bottom) / 2;

    kb::editor::tests::Require(kb::editor::EditorAssetBrowserHitTester::PrefabAssetAt(content, x, y, state, manager).has_value(), "Asset browser should allow legacy .kbprefab assets to start prefab drag");
    const std::optional<kb::assets::AssetMetadata> metadata = kb::editor::EditorAssetBrowserHitTester::AssetMetadataAt(content, x, y, state, manager);
    kb::editor::tests::Require(metadata.has_value() && metadata->type == "LegacyPrefab", "Asset browser should expose metadata for every draggable asset tile");
}

void RunContextMenuHitTestTest() {
    kb::assets::AssetManager manager;
    static_cast<void>(manager.RegisterAsset(Metadata("Main_Camera", "ScenePrefab", "/Game/Prefabs/Main_Camera.kbprefab")));
    const kb::assets::AssetId id = manager.Registry().FindByPath("/Game/Prefabs/Main_Camera.kbprefab")->id;

    kb::editor::EditorAssetBrowserState state;
    kb::editor::tests::Require(state.OpenContextMenuForAsset(220, 70, id, manager), "Asset browser should open context menu for a registered asset");
    const RECT content{ 0, 0, 640, 240 };
    const RECT menu = kb::editor::EditorAssetBrowserLayout::ContextMenuRect(content, state.ContextMenuX(), state.ContextMenuY(), static_cast<int>(state.ContextMenuItems(manager).size()));
    const RECT rename = kb::editor::EditorAssetBrowserLayout::ContextMenuItemRect(menu, 0);
    const kb::editor::EditorAssetBrowserHit hit = kb::editor::EditorAssetBrowserHitTester::HitTest(content, (rename.left + rename.right) / 2, (rename.top + rename.bottom) / 2, state, manager);

    kb::editor::tests::Require(hit.kind == kb::editor::EditorAssetBrowserHitKind::ContextMenuCommand, "Asset browser context menu row should be hit-testable");
    kb::editor::tests::Require(hit.command == kb::editor::EditorAssetContextCommand::Rename, "Asset browser first asset context command should rename");
}

void RunImportCommandHitTestTest() {
    kb::assets::AssetManager manager;
    kb::editor::EditorAssetBrowserState state;
    const RECT content{ 0, 0, 720, 240 };
    const kb::editor::EditorAssetBrowserLayoutRects layout = kb::editor::EditorAssetBrowserLayout::Build(content);
    const kb::editor::EditorAssetBrowserHit toolbarHit = kb::editor::EditorAssetBrowserHitTester::HitTest(
        content,
        (layout.importButton.left + layout.importButton.right) / 2,
        (layout.importButton.top + layout.importButton.bottom) / 2,
        state,
        manager);
    kb::editor::tests::Require(toolbarHit.kind == kb::editor::EditorAssetBrowserHitKind::Import, "Asset browser Import toolbar button should be hit-testable");

    state.OpenContextMenuForBackground(220, 70);
    const std::vector<kb::editor::EditorAssetContextMenuItem> items = state.ContextMenuItems(manager);
    kb::editor::tests::Require(!items.empty() && items.front().command == kb::editor::EditorAssetContextCommand::Import, "Asset browser background context menu should expose Import first");
}

void RunMaterialContextMenuCommandTest() {
    kb::assets::AssetManager manager;
    static_cast<void>(manager.RegisterAsset(Metadata("Stone", "ScenePrefab", "/Game/Environment/Stone.kbprefab")));
    static_cast<void>(manager.RegisterAsset(Metadata("Character", "RenderMesh", "/Game/Environment/Character.gltf")));
    kb::editor::EditorAssetBrowserState state;

    state.OpenContextMenuForBackground(220, 70);
    const std::vector<kb::editor::EditorAssetContextMenuItem> backgroundItems = state.ContextMenuItems(manager);
    const bool backgroundHasMaterial = std::ranges::any_of(backgroundItems, [](const kb::editor::EditorAssetContextMenuItem& item) {
        return item.command == kb::editor::EditorAssetContextCommand::NewMaterial;
    });
    kb::editor::tests::Require(backgroundHasMaterial, "Asset browser background context menu should expose New Material");

    kb::editor::tests::Require(state.OpenContextMenuForFolder(220, 70, "/Game/Environment", manager), "Asset browser should open a folder context menu for registered virtual folders");
    const std::vector<kb::editor::EditorAssetContextMenuItem> folderItems = state.ContextMenuItems(manager);
    const bool folderHasMaterial = std::ranges::any_of(folderItems, [](const kb::editor::EditorAssetContextMenuItem& item) {
        return item.command == kb::editor::EditorAssetContextCommand::NewMaterial;
    });
    kb::editor::tests::Require(folderHasMaterial, "Asset browser folder context menu should expose New Material");

    static_cast<void>(manager.RegisterAsset(Metadata("Paint", "RenderMaterial", "/Game/Environment/Paint.kbmat")));
    const kb::assets::AssetMetadata* material = manager.Registry().FindByPath("/Game/Environment/Paint.kbmat");
    kb::editor::tests::Require(material != nullptr, "Asset browser material command test did not register material asset");
    kb::editor::tests::Require(state.OpenContextMenuForAsset(220, 70, material->id, manager), "Asset browser should open a material asset context menu");
    const std::vector<kb::editor::EditorAssetContextMenuItem> materialItems = state.ContextMenuItems(manager);
    const std::vector<kb::editor::EditorAssetContextCommand> expectedMaterialCommands{
        kb::editor::EditorAssetContextCommand::Open,
        kb::editor::EditorAssetContextCommand::Duplicate,
        kb::editor::EditorAssetContextCommand::CreateMaterialInstance,
        kb::editor::EditorAssetContextCommand::Rename,
        kb::editor::EditorAssetContextCommand::Delete,
        kb::editor::EditorAssetContextCommand::FindReferences,
        kb::editor::EditorAssetContextCommand::Refresh,
    };
    kb::editor::tests::Require(materialItems.size() == expectedMaterialCommands.size(), "Material asset context menu should expose the production material command set");
    for (std::size_t index = 0; index < expectedMaterialCommands.size(); ++index) {
        kb::editor::tests::Require(materialItems[index].command == expectedMaterialCommands[index], "Material asset context menu command order is incorrect");
    }

    const kb::assets::AssetMetadata* mesh = manager.Registry().FindByPath("/Game/Environment/Character.gltf");
    kb::editor::tests::Require(mesh != nullptr, "Asset browser material command test did not register mesh asset");
    kb::editor::tests::Require(state.OpenContextMenuForAsset(220, 70, mesh->id, manager), "Asset browser should open a mesh asset context menu");
    const std::vector<kb::editor::EditorAssetContextMenuItem> meshItems = state.ContextMenuItems(manager);
    kb::editor::tests::Require(!meshItems.empty() && meshItems.front().command == kb::editor::EditorAssetContextCommand::ExtractMaterials, "Mesh asset context menu should expose Extract Materials first");
}

void RunMaterialAssetDoubleClickOpensMaterialEditorTest() {
    kb::assets::AssetManager manager;
    static_cast<void>(manager.RegisterAsset(Metadata("PreviewMaterial", "RenderMaterial", "/Game/Materials/PreviewMaterial.kbmat")));
    static_cast<void>(manager.RegisterAsset(Metadata("PreviewMaterialInstance", "RenderMaterialInstance", "/Game/Materials/PreviewMaterialInstance.kbmatinst")));

    kb::editor::EditorAssetBrowserState state;
    kb::editor::tests::Require(state.SelectFolder("/Game/Materials", manager), "Material double-click test should open the material folder");

    const kb::assets::AssetMetadata* material = manager.Registry().FindByPath("/Game/Materials/PreviewMaterial.kbmat");
    kb::editor::tests::Require(material != nullptr, "Material double-click test did not register material metadata");
    const kb::editor::EditorAssetBrowserDoubleClickResult result =
        kb::editor::EditorAssetBrowserDoubleClickHandler::HandleMaterialAssetDoubleClick(*material, state, manager);
    kb::editor::tests::Require(result == kb::editor::EditorAssetBrowserDoubleClickResult::MaterialEditorOpened, "Double-clicking a material asset should request Material Editor activation");
    kb::editor::tests::Require(state.InspectorAsset() == material->id, "Double-clicking a material asset should select it for the Material Editor");

    const kb::assets::AssetMetadata* instance = manager.Registry().FindByPath("/Game/Materials/PreviewMaterialInstance.kbmatinst");
    kb::editor::tests::Require(instance != nullptr, "Material double-click test did not register material instance metadata");
    const kb::editor::EditorAssetBrowserDoubleClickResult instanceResult =
        kb::editor::EditorAssetBrowserDoubleClickHandler::HandleMaterialAssetDoubleClick(*instance, state, manager);
    kb::editor::tests::Require(instanceResult == kb::editor::EditorAssetBrowserDoubleClickResult::MaterialEditorOpened, "Double-clicking a material instance asset should request Material Editor activation");
    kb::editor::tests::Require(state.InspectorAsset() == instance->id, "Double-clicking a material instance asset should select it for the Material Editor");
}

void RunMaterialAssetIconResolverRecognizesPreviewMaterialsTest() {
    const kb::assets::AssetMetadata material = Metadata("StudioPaint", "RenderMaterial", "/Game/Materials/StudioPaint.kbmat");
    const kb::assets::AssetMetadata instance = Metadata("StudioPaint_Inst", "RenderMaterialInstance", "/Game/Materials/StudioPaint_Inst.kbmatinst");
    const kb::assets::AssetMetadata mesh = Metadata("StudioMesh", "RenderMesh", "/Game/Meshes/StudioMesh.gltf");

    kb::editor::tests::Require(kb::editor::ProjectFilesAssetIconResolver::IsMaterial(material), "Project Files should classify material assets for the preview thumbnail path");
    kb::editor::tests::Require(kb::editor::ProjectFilesAssetIconResolver::IsMaterial(instance), "Project Files should classify material instances for the preview thumbnail path");
    kb::editor::tests::Require(!kb::editor::ProjectFilesAssetIconResolver::IsMaterial(mesh), "Project Files should keep mesh assets on the mesh thumbnail path");
}
#endif

} // namespace

namespace kb::editor::tests {

void RunEditorAssetBrowserTests() {
    RunFolderTreeTest();
    RunFilteringTest();
    RunMaterialSearchAndFilterTest();
    RunSelectionAndTypeCycleTest();
    RunMultiSelectionStateTest();
    RunTextEditStateTest();
    RunSearchTextShortcutStateTest();
#if defined(_WIN32)
    RunProjectFilesEdgeToEdgeLayoutTest();
    RunTileHitTestUsesExactGridGeometryTest();
    RunTreeSplitterHitTestTest();
    RunTreeDisclosureHitTestTest();
    RunContentFolderHitTestExposesFolderDragSourceTest();
    RunLegacyPrefabExtensionStillDraggableTest();
    RunContextMenuHitTestTest();
    RunImportCommandHitTestTest();
    RunMaterialContextMenuCommandTest();
    RunMaterialAssetDoubleClickOpensMaterialEditorTest();
    RunMaterialAssetIconResolverRecognizesPreviewMaterialsTest();
#endif
}

} // namespace kb::editor::tests
