#include "EditorTestSupport.hpp"
#include "EditorTestSuites.hpp"

#include "assets/EditorAssetBrowserHitTester.hpp"
#include "assets/EditorAssetBrowserLayout.hpp"
#include "assets/EditorAssetBrowserState.hpp"
#include "app/EditorAssetBrowserDoubleClickHandler.hpp"
#include "engine/assets/AssetManager.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneAssets.hpp"
#include "engine/scene/SkeletalMeshAssetIO.hpp"
#include "scene/EditorSceneAssetBrowserCommands.hpp"
#include "kb/render/resources/RenderMaterialAssetLoader.hpp"
#include "kb/render/resources/RenderTextureAssetLoader.hpp"
#if defined(_WIN32)
#include "kb/render/resources/RenderMaterialAssetWriter.hpp"
#include "rendering/ProjectFilesAssetIconResolver.hpp"
#include "rendering/ProjectFilesMaterialPreviewThumbnailModel.hpp"
#include "rendering/ProjectFilesMaterialPreviewThumbnailPolicy.hpp"
#endif

#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

[[nodiscard]] std::filesystem::path TempRoot() {
    return std::filesystem::temp_directory_path() / "21kb_editor_asset_browser_tests";
}

void ResetTempRoot() {
    std::error_code error;
    std::filesystem::remove_all(TempRoot(), error);
}

void WriteTextFile(const std::filesystem::path& path, std::string_view text) {
    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);
    std::ofstream output{ path, std::ios::binary | std::ios::trunc };
    output << text;
}

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
    static_cast<void>(manager.RegisterAsset(Metadata("LayeredSurface", "RenderMaterialGraph", "/Game/Materials/LayeredSurface.kbmaterialgraph")));
    static_cast<void>(manager.RegisterAsset(Metadata("LayeredSurfaceType", "RenderMaterialType", "/Game/Materials/LayeredSurfaceType.kbmaterialtype")));
    static_cast<void>(manager.RegisterAsset(Metadata("HeroMesh", "RenderMesh", "/Game/Characters/HeroMesh.gltf")));
    static_cast<void>(manager.RegisterAsset(Metadata("BrushTexture", "RenderTexture", "/Game/Props/BrushTexture.ktx")));

    kb::editor::EditorAssetBrowserState state;
    static_cast<void>(state.SelectFolder("/Game", manager));
    state.SetRecursive(true);
    state.SetSearchQuery("materialy");
    std::vector<kb::editor::EditorAssetItemRow> assets = state.AssetRows(manager);
    kb::editor::tests::Require(assets.size() == 4, "Asset browser material search should find every material asset type");
    kb::editor::tests::Require(std::ranges::any_of(assets, [](const kb::editor::EditorAssetItemRow& row) { return row.metadata.type == "RenderMaterial"; }), "Asset browser material search missed RenderMaterial");
    kb::editor::tests::Require(std::ranges::any_of(assets, [](const kb::editor::EditorAssetItemRow& row) { return row.metadata.type == "RenderMaterialInstance"; }), "Asset browser material search missed RenderMaterialInstance");
    kb::editor::tests::Require(std::ranges::any_of(assets, [](const kb::editor::EditorAssetItemRow& row) { return row.metadata.type == "RenderMaterialGraph"; }), "Asset browser material search missed RenderMaterialGraph");
    kb::editor::tests::Require(std::ranges::any_of(assets, [](const kb::editor::EditorAssetItemRow& row) { return row.metadata.type == "RenderMaterialType"; }), "Asset browser material search missed RenderMaterialType");
    kb::editor::tests::Require(std::ranges::none_of(assets, [](const kb::editor::EditorAssetItemRow& row) { return row.metadata.type == "RenderMesh" || row.metadata.type == "RenderTexture"; }), "Asset browser material search should not include non-material assets");

    const std::vector<std::string> types = state.AssetTypes(manager);
    kb::editor::tests::Require(std::ranges::find(types, "Materials") != types.end(), "Asset browser type filters should expose a combined Materials filter");
    for (std::size_t index = 0; index <= types.size() && state.TypeFilter() != "Materials"; ++index) {
        state.CycleTypeFilter(manager);
    }
    kb::editor::tests::Require(state.TypeFilter() == "Materials", "Asset browser type cycling should reach the combined Materials filter");
    state.ClearSearch();
    assets = state.AssetRows(manager);
    kb::editor::tests::Require(assets.size() == 4, "Asset browser Materials filter should include material documents, graph assets and type assets");
    kb::editor::tests::Require(std::ranges::all_of(assets, [](const kb::editor::EditorAssetItemRow& row) {
        return row.metadata.type == "RenderMaterial" ||
            row.metadata.type == "RenderMaterialInstance" ||
            row.metadata.type == "RenderMaterialGraph" ||
            row.metadata.type == "RenderMaterialType";
    }), "Asset browser Materials filter should exclude non-material-family assets");
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

void RunDependencySafetyBlocksRenameAndDeleteTest() {
    kb::scene::Scene scene;
    kb::assets::AssetManager& manager = scene.Assets().Manager();
    static_cast<void>(manager.RegisterAsset(Metadata("GraphSurface", "RenderMaterialType", "/Game/MaterialTypes/GraphSurface.kbmaterialtype")));
    const kb::assets::AssetMetadata* type = manager.Registry().FindByPath("/Game/MaterialTypes/GraphSurface.kbmaterialtype");
    kb::editor::tests::Require(type != nullptr, "KBMAT-GRAPH-0005: Dependency safety test did not register Material Type asset");
    const kb::assets::AssetId typeId = type->id;

    kb::assets::AssetMetadata material = Metadata("GraphBacked", "RenderMaterial", "/Game/Materials/GraphBacked.kbmat");
    material.dependencies.push_back(typeId);
    static_cast<void>(manager.RegisterAsset(std::move(material)));

    kb::editor::EditorAssetBrowserState state;
    static_cast<void>(state.SelectFolder("/Game/MaterialTypes", manager));
    kb::editor::tests::Require(state.BeginRenameAsset(typeId, manager), "KBMAT-GRAPH-0005: Dependency safety test could not enter rename mode");
    state.SetTextEditValue("RenamedGraphSurface");
    kb::editor::tests::Require(!kb::editor::EditorSceneAssetBrowserCommands::CommitTextEdit(scene, state),
        "KBMAT-GRAPH-0005: Project Files should block renaming an asset referenced by another asset dependency");
    kb::editor::tests::Require(manager.LastError().find("depends on it") != std::string::npos,
        "KBMAT-GRAPH-0005: Rename dependency safety should report the dependent asset");

    kb::editor::tests::Require(!kb::editor::EditorSceneAssetBrowserCommands::DeleteAsset(scene, state, typeId),
        "KBMAT-GRAPH-0005: Project Files should block deleting an asset referenced by another asset dependency");
    kb::editor::tests::Require(manager.Registry().Find(typeId) != nullptr,
        "KBMAT-GRAPH-0005: Blocked dependency delete should keep the target asset registered");
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

void RunImportCommandReturnsMaterialTextureReportTest() {
    ResetTempRoot();

    const std::filesystem::path projectRoot = TempRoot() / "Project";
    const std::filesystem::path sourceRoot = TempRoot() / "Sources";
    WriteTextFile(sourceRoot / "Albedo.png", "texture bytes");
    WriteTextFile(sourceRoot / "Paint.mtl", "material bytes");
    WriteTextFile(sourceRoot / "Unsupported.assetx", "unsupported bytes");

    kb::scene::Scene scene;
    kb::editor::EditorAssetBrowserState browser;
    kb::editor::tests::Require(scene.Assets().MountProject(projectRoot), "Import command report test could not mount project assets");

    const std::array<std::filesystem::path, 2U> firstFiles{
        sourceRoot / "Albedo.png",
        sourceRoot / "Paint.mtl",
    };
    const kb::assets::AssetImportResult first =
        kb::editor::EditorSceneAssetBrowserCommands::ImportFilesWithReport(scene, browser, firstFiles, "/Game/Imports");
    kb::editor::tests::Require(first.Succeeded(), "Import command report test could not import initial material and texture");
    kb::editor::tests::Require(first.CreatedCount() == 2U && first.ReusedCount() == 0U, "Import command did not report created material and texture assets");

    const std::array<std::filesystem::path, 4U> mixedFiles{
        sourceRoot / "Albedo.png",
        sourceRoot / "Paint.mtl",
        sourceRoot / "Missing.png",
        sourceRoot / "Unsupported.assetx",
    };
    const kb::assets::AssetImportResult report =
        kb::editor::EditorSceneAssetBrowserCommands::ImportFilesWithReport(scene, browser, mixedFiles, "/Game/Imports");
    kb::editor::tests::Require(report.ImportedCount() == 2U, "Import command report should treat reused material and texture assets as imported");
    kb::editor::tests::Require(report.CreatedCount() == 0U, "Import command report should not create duplicate material or texture assets");
    kb::editor::tests::Require(report.ReusedCount() == 2U, "Import command report did not expose reused material and texture assets");
    kb::editor::tests::Require(report.MissingCount() == 1U, "Import command report did not expose missing texture source");
    kb::editor::tests::Require(report.UnsupportedCount() == 1U, "Import command report did not expose unsupported source");
    kb::editor::tests::Require(report.items[0].category == kb::assets::AssetImportCategory::Texture && report.items[0].status == kb::assets::AssetImportItemStatus::Reused, "Import command report did not preserve reused texture info");
    kb::editor::tests::Require(report.items[1].category == kb::assets::AssetImportCategory::Material && report.items[1].status == kb::assets::AssetImportItemStatus::Reused, "Import command report did not preserve reused material info");
    kb::editor::tests::Require(report.items[2].category == kb::assets::AssetImportCategory::Texture && report.items[2].status == kb::assets::AssetImportItemStatus::Missing, "Import command report did not preserve missing texture info");
    kb::editor::tests::Require(report.items[3].category == kb::assets::AssetImportCategory::Unknown && report.items[3].status == kb::assets::AssetImportItemStatus::Unsupported, "Import command report did not preserve unsupported info");
    kb::editor::tests::Require(browser.SelectedAsset() == first.items.front().id, "Import command should select the first imported or reused asset");

    ResetTempRoot();
}

void WriteSkeletalGltfImportFixture(const std::filesystem::path& folder) {
    std::error_code error;
    std::filesystem::create_directories(folder, error);
    const std::array<float, 9U> positions{ 0.0F, 0.0F, 0.0F, 1.0F, 0.0F, 0.0F, 0.0F, 1.0F, 0.0F };
    const std::array<std::uint16_t, 12U> joints{ 0U, 0U, 0U, 0U, 1U, 0U, 0U, 0U, 1U, 0U, 0U, 0U };
    const std::array<float, 12U> weights{ 1.0F, 0.0F, 0.0F, 0.0F, 1.0F, 0.0F, 0.0F, 0.0F, 1.0F, 0.0F, 0.0F, 0.0F };
    const std::array<std::uint16_t, 3U> indices{ 0U, 1U, 2U };
    const std::array<float, 32U> inverseBinds{
        1.0F, 0.0F, 0.0F, 0.0F, 0.0F, 1.0F, 0.0F, 0.0F, 0.0F, 0.0F, 1.0F, 0.0F, 0.0F, 0.0F, 0.0F, 1.0F,
        1.0F, 0.0F, 0.0F, 0.0F, 0.0F, 1.0F, 0.0F, 0.0F, 0.0F, 0.0F, 1.0F, 0.0F, 0.0F, -1.0F, 0.0F, 1.0F,
    };
    std::ofstream binary{ folder / "Robot.bin", std::ios::binary | std::ios::trunc };
    binary.write(reinterpret_cast<const char*>(positions.data()), sizeof(positions));
    binary.write(reinterpret_cast<const char*>(joints.data()), sizeof(joints));
    binary.write(reinterpret_cast<const char*>(weights.data()), sizeof(weights));
    binary.write(reinterpret_cast<const char*>(indices.data()), sizeof(indices));
    binary.write(reinterpret_cast<const char*>(inverseBinds.data()), sizeof(inverseBinds));
    WriteTextFile(folder / "Robot.gltf", R"({"asset":{"version":"2.0"},"buffers":[{"uri":"Robot.bin","byteLength":242}],"bufferViews":[{"buffer":0,"byteOffset":0,"byteLength":36},{"buffer":0,"byteOffset":36,"byteLength":24},{"buffer":0,"byteOffset":60,"byteLength":48},{"buffer":0,"byteOffset":108,"byteLength":6},{"buffer":0,"byteOffset":114,"byteLength":128}],"accessors":[{"bufferView":0,"componentType":5126,"count":3,"type":"VEC3"},{"bufferView":1,"componentType":5123,"count":3,"type":"VEC4"},{"bufferView":2,"componentType":5126,"count":3,"type":"VEC4"},{"bufferView":3,"componentType":5123,"count":3,"type":"SCALAR"},{"bufferView":4,"componentType":5126,"count":2,"type":"MAT4"}],"meshes":[{"primitives":[{"attributes":{"POSITION":0,"JOINTS_0":1,"WEIGHTS_0":2},"indices":3}]}],"nodes":[{"name":"Root","children":[1]},{"name":"Spine","translation":[0,1,0]},{"mesh":0,"skin":0}],"skins":[{"joints":[0,1],"inverseBindMatrices":4}]})");
}

void RunImportCommandPublishesSkeletalGltfTest() {
    ResetTempRoot();
    const std::filesystem::path projectRoot = TempRoot() / "SkeletalProject";
    const std::filesystem::path sourceRoot = TempRoot() / "SkeletalSources";
    WriteSkeletalGltfImportFixture(sourceRoot);

    kb::scene::Scene scene;
    kb::editor::EditorAssetBrowserState browser;
    kb::editor::tests::Require(scene.Assets().MountProject(projectRoot), "Skeletal import command test could not mount project assets");
    kb::assets::AssetImportOptions options{};
    options.mesh.importSkeletalMesh = true;
    const std::array<std::filesystem::path, 1U> files{ sourceRoot / "Robot.gltf" };
    const kb::assets::AssetImportResult report =
        kb::editor::EditorSceneAssetBrowserCommands::ImportFilesWithReport(scene, browser, files, "/Game/Characters", options);
    const kb::assets::AssetMetadata* mesh = scene.Assets().Manager().Registry().FindByPath("/Game/Characters/Robot.kbskeletalmesh");
    const kb::assets::AssetMetadata* skeleton = scene.Assets().Manager().Registry().FindByPath("/Game/Characters/Robot.kbskeleton");
    kb::editor::tests::Require(report.Succeeded() && report.CreatedCount() == 1U && mesh != nullptr &&
            mesh->type == "SkeletalMesh" && skeleton != nullptr && skeleton->type == "Skeleton" &&
            browser.SelectedAsset() == mesh->id,
        "Skeletal glTF import command did not publish and select the Skeletal Mesh and Skeleton assets");
    ResetTempRoot();
}

void WriteSkeletalGltfMaterialAndTextureFixture(const std::filesystem::path& folder) {
    WriteSkeletalGltfImportFixture(folder);
    std::filesystem::path current = std::filesystem::current_path();
    std::filesystem::path pngFixture;
    while (!current.empty()) {
        const std::filesystem::path candidate =
            current / "third_party/bgfx.cmake/bgfx/examples/runtime/images/SplashScreen.png";
        if (std::filesystem::is_regular_file(candidate)) {
            pngFixture = candidate;
            break;
        }
        const std::filesystem::path parent = current.parent_path();
        if (parent == current) {
            break;
        }
        current = parent;
    }
    kb::editor::tests::Require(!pngFixture.empty(), "Skeletal material import PNG fixture was not found");
    std::error_code copyError;
    static_cast<void>(std::filesystem::copy_file(
        pngFixture, folder / "Albedo.png", std::filesystem::copy_options::overwrite_existing, copyError));
    kb::editor::tests::Require(!copyError, "Skeletal material import PNG fixture could not be copied");
    WriteTextFile(folder / "Robot.gltf", R"({"asset":{"version":"2.0"},"buffers":[{"uri":"Robot.bin","byteLength":242}],"bufferViews":[{"buffer":0,"byteOffset":0,"byteLength":36},{"buffer":0,"byteOffset":36,"byteLength":24},{"buffer":0,"byteOffset":60,"byteLength":48},{"buffer":0,"byteOffset":108,"byteLength":6},{"buffer":0,"byteOffset":114,"byteLength":128}],"accessors":[{"bufferView":0,"componentType":5126,"count":3,"type":"VEC3"},{"bufferView":1,"componentType":5123,"count":3,"type":"VEC4"},{"bufferView":2,"componentType":5126,"count":3,"type":"VEC4"},{"bufferView":3,"componentType":5123,"count":3,"type":"SCALAR"},{"bufferView":4,"componentType":5126,"count":2,"type":"MAT4"}],"images":[{"name":"Albedo","uri":"Albedo.png"}],"textures":[{"source":0}],"materials":[{"name":"RobotSurface","pbrMetallicRoughness":{"baseColorTexture":{"index":0},"metallicFactor":0.25,"roughnessFactor":0.75}}],"meshes":[{"name":"RobotParts","primitives":[{"attributes":{"POSITION":0,"JOINTS_0":1,"WEIGHTS_0":2},"indices":3,"material":0}]}],"nodes":[{"name":"Root","children":[1]},{"name":"Spine","translation":[0,1,0]},{"name":"Body","mesh":0,"skin":0},{"name":"Armor","mesh":0,"skin":0}],"skins":[{"joints":[0,1],"inverseBindMatrices":4}]})");
}

void RunImportCommandPublishesCombinedSkeletalMeshMaterialsAndTexturesTest() {
    ResetTempRoot();
    const std::filesystem::path projectRoot = TempRoot() / "SkeletalMaterialProject";
    const std::filesystem::path sourceRoot = TempRoot() / "SkeletalMaterialSources";
    WriteSkeletalGltfMaterialAndTextureFixture(sourceRoot);

    kb::scene::Scene scene;
    kb::editor::EditorAssetBrowserState browser;
    kb::editor::tests::Require(scene.Assets().MountProject(projectRoot),
        "Skeletal material import test could not mount project assets");
    kb::assets::AssetImportOptions options{};
    options.mesh.importSkeletalMesh = true;
    options.mesh.importTextures = true;
    options.mesh.importMaterials = true;
    options.mesh.combineMeshes = true;
    const std::array<std::filesystem::path, 1U> files{ sourceRoot / "Robot.gltf" };
    const kb::assets::AssetImportResult report =
        kb::editor::EditorSceneAssetBrowserCommands::ImportFilesWithReport(
            scene, browser, files, "/Game/Characters", options);

    const kb::assets::AssetMetadata* meshMetadata =
        scene.Assets().Manager().Registry().FindByPath("/Game/Characters/Robot.kbskeletalmesh");
    const std::vector<kb::assets::AssetMetadata> materials =
        scene.Assets().Manager().Registry().ByType("RenderMaterial");
    const std::vector<kb::assets::AssetMetadata> textures =
        scene.Assets().Manager().Registry().ByType("RenderTexture");
    const auto mesh = meshMetadata == nullptr
        ? std::nullopt
        : kb::scene::SkeletalMeshAssetIO::Load(meshMetadata->physicalPath);
    const auto material = materials.size() == 1U
        ? kb::render::RenderMaterialAssetLoader::LoadMaterial(materials[0].physicalPath)
        : std::nullopt;
    kb::editor::tests::Require(material.has_value(),
        "Skeletal mesh import did not publish a loadable material asset");
    const auto texture = textures.size() == 1U
        ? kb::render::RenderTextureAssetLoader::LoadTexture(textures[0].physicalPath)
        : std::nullopt;
    kb::editor::tests::Require(texture.has_value(),
        "Skeletal mesh import did not publish a loadable texture asset");
    const bool assigned = mesh.has_value() && materials.size() == 1U &&
        mesh->lods.size() == 1U && mesh->lods[0].vertices.size() == 6U &&
        mesh->lods[0].sections.size() == 2U &&
        std::ranges::all_of(mesh->lods[0].sections, [&](const kb::scene::SkeletalMeshSection& section) {
            return section.materialAssetId == materials[0].id.value;
        });
    kb::editor::tests::Require(report.Succeeded() && report.CreatedCount() == 1U &&
            materials.size() == 1U && textures.size() == 1U && assigned &&
            material->desc.albedoTextureAssetId == textures[0].id.value,
        "Skeletal mesh import did not combine nodes and publish assigned material and texture assets");
    ResetTempRoot();
}

#if defined(_WIN32)
[[nodiscard]] kb::assets::AssetMetadata MaterialMetadata(std::string name, const std::filesystem::path& path, std::uint64_t contentHash) {
    kb::assets::AssetMetadata metadata = Metadata(std::move(name), "RenderMaterial", "/Game/Materials/ThumbnailProbe.kbmat");
    metadata.id.value = 0x2100BEEF + contentHash;
    metadata.physicalPath = path;
    metadata.contentHash = contentHash;
    return metadata;
}

void WritePreviewMaterial(const std::filesystem::path& path, float red, float green, float blue) {
    kb::render::RenderMaterialAssetData material{};
    material.desc.baseColor[0] = red;
    material.desc.baseColor[1] = green;
    material.desc.baseColor[2] = blue;
    material.desc.baseColor[3] = 1.0F;
    material.desc.roughnessFactor = 0.42F;
    material.desc.emissiveStrength = 0.0F;
    kb::editor::tests::Require(kb::render::RenderMaterialAssetWriter::Save(path, material), "KBMAT-UE-0015: Could not write material thumbnail fixture");
}

[[nodiscard]] bool HasVisiblePreviewPixels(const kb::editor::ProjectFilesMaterialPreviewImage& image) {
    if (image.width <= 0 || image.height <= 0 || image.bgra.empty()) {
        return false;
    }
    const std::uint32_t first = image.bgra.front();
    std::size_t differentPixels = 0U;
    for (const std::uint32_t pixel : image.bgra) {
        if (pixel != first && ++differentPixels >= 32U) {
            return true;
        }
    }
    return false;
}

[[nodiscard]] std::size_t CountErrorTintPixels(const kb::editor::ProjectFilesMaterialPreviewImage& image) {
    std::size_t count = 0U;
    for (const std::uint32_t pixel : image.bgra) {
        const int blue = static_cast<int>(pixel & 0xFFU);
        const int green = static_cast<int>((pixel >> 8U) & 0xFFU);
        const int red = static_cast<int>((pixel >> 16U) & 0xFFU);
        if (red > green + 32 && red > blue + 12) {
            ++count;
        }
    }
    return count;
}

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
    const bool backgroundHasMaterialGraph = std::ranges::any_of(backgroundItems, [](const kb::editor::EditorAssetContextMenuItem& item) {
        return item.command == kb::editor::EditorAssetContextCommand::NewMaterialGraph;
    });
    const bool backgroundHasMaterialType = std::ranges::any_of(backgroundItems, [](const kb::editor::EditorAssetContextMenuItem& item) {
        return item.command == kb::editor::EditorAssetContextCommand::NewMaterialType;
    });
    // New Material is the single graph-backed material entry (UE-style: double-click opens the graph editor);
    // the standalone "New Material Graph" creation entry was removed to avoid a dead double-click.
    kb::editor::tests::Require(!backgroundHasMaterialGraph, "Asset browser background context menu must not expose the standalone New Material Graph creation entry");
    kb::editor::tests::Require(backgroundHasMaterialType, "Asset browser background context menu should expose Material Type creation");

    kb::editor::tests::Require(state.OpenContextMenuForFolder(220, 70, "/Game/Environment", manager), "Asset browser should open a folder context menu for registered virtual folders");
    const std::vector<kb::editor::EditorAssetContextMenuItem> folderItems = state.ContextMenuItems(manager);
    const bool folderHasMaterial = std::ranges::any_of(folderItems, [](const kb::editor::EditorAssetContextMenuItem& item) {
        return item.command == kb::editor::EditorAssetContextCommand::NewMaterial;
    });
    kb::editor::tests::Require(folderHasMaterial, "Asset browser folder context menu should expose New Material");
    kb::editor::tests::Require(std::ranges::none_of(folderItems, [](const kb::editor::EditorAssetContextMenuItem& item) {
            return item.command == kb::editor::EditorAssetContextCommand::NewMaterialGraph;
        }),
        "Asset browser folder context menu must not expose the standalone New Material Graph creation entry");
    kb::editor::tests::Require(std::ranges::any_of(folderItems, [](const kb::editor::EditorAssetContextMenuItem& item) {
            return item.command == kb::editor::EditorAssetContextCommand::NewMaterialType;
        }),
        "Asset browser folder context menu should expose Material Type creation");

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

    static_cast<void>(manager.RegisterAsset(Metadata("PaintGraph", "RenderMaterialGraph", "/Game/Environment/PaintGraph.kbmaterialgraph")));
    static_cast<void>(manager.RegisterAsset(Metadata("PaintType", "RenderMaterialType", "/Game/Environment/PaintType.kbmaterialtype")));
    const kb::assets::AssetMetadata* graph = manager.Registry().FindByPath("/Game/Environment/PaintGraph.kbmaterialgraph");
    const kb::assets::AssetMetadata* type = manager.Registry().FindByPath("/Game/Environment/PaintType.kbmaterialtype");
    kb::editor::tests::Require(graph != nullptr && type != nullptr, "Asset browser material command test did not register graph/type assets");
    kb::editor::tests::Require(state.OpenContextMenuForAsset(220, 70, graph->id, manager), "Asset browser should open a Material Graph context menu");
    const std::vector<kb::editor::EditorAssetContextMenuItem> graphItems = state.ContextMenuItems(manager);
    kb::editor::tests::Require(graphItems.size() >= 2U &&
            graphItems[0].command == kb::editor::EditorAssetContextCommand::Open &&
            graphItems[1].command == kb::editor::EditorAssetContextCommand::CreateMaterialFromGraph,
        "P1.9: Material Graph context menu should expose Open and Create Material From Graph");
    kb::editor::tests::Require(std::ranges::any_of(graphItems, [](const kb::editor::EditorAssetContextMenuItem& item) {
            return item.command == kb::editor::EditorAssetContextCommand::Open;
        }),
        "P1.9: Material Graph context menu should expose Material Editor Open");
    kb::editor::tests::Require(state.OpenContextMenuForAsset(220, 70, type->id, manager), "Asset browser should open a Material Type context menu");
    const std::vector<kb::editor::EditorAssetContextMenuItem> typeItems = state.ContextMenuItems(manager);
    kb::editor::tests::Require(!typeItems.empty() && typeItems.front().command == kb::editor::EditorAssetContextCommand::CreateMaterialFromMaterialType,
        "Material Type context menu should expose Create Material From Material Type first");
    kb::editor::tests::Require(std::ranges::none_of(typeItems, [](const kb::editor::EditorAssetContextMenuItem& item) {
            return item.command == kb::editor::EditorAssetContextCommand::Open;
        }),
        "Material Type context menu should not expose Material Editor Open");

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
    const kb::assets::AssetMetadata graph = Metadata("StudioGraph", "RenderMaterialGraph", "/Game/Materials/StudioGraph.kbmaterialgraph");
    const kb::assets::AssetMetadata type = Metadata("StudioType", "RenderMaterialType", "/Game/Materials/StudioType.kbmaterialtype");
    const kb::assets::AssetMetadata mesh = Metadata("StudioMesh", "RenderMesh", "/Game/Meshes/StudioMesh.gltf");

    kb::editor::tests::Require(kb::editor::ProjectFilesAssetIconResolver::IsMaterial(material), "Project Files should classify material assets for the preview thumbnail path");
    kb::editor::tests::Require(kb::editor::ProjectFilesAssetIconResolver::IsMaterial(instance), "Project Files should classify material instances for the preview thumbnail path");
    kb::editor::tests::Require(!kb::editor::ProjectFilesAssetIconResolver::IsMaterial(graph), "Project Files should not treat raw Material Graph assets as assignable material previews");
    kb::editor::tests::Require(!kb::editor::ProjectFilesAssetIconResolver::IsMaterial(type), "Project Files should not treat Material Type assets as assignable material previews");
    kb::editor::tests::Require(kb::editor::ProjectFilesAssetIconResolver::IsMaterialGraph(graph), "Project Files should classify Material Graph assets");
    kb::editor::tests::Require(kb::editor::ProjectFilesAssetIconResolver::IsMaterialType(type), "Project Files should classify Material Type assets");
    kb::editor::tests::Require(!kb::editor::ProjectFilesAssetIconResolver::IsMaterial(mesh), "Project Files should keep mesh assets on the mesh thumbnail path");
    kb::editor::tests::Require(kb::editor::ProjectFilesAssetIconResolver::Resolve(graph, false).kind == kb::editor::HeroIconKind::RectangleGroup,
        "KBMAT-GRAPH-0005: Material Graph should use a graph/document icon instead of the material preview sphere");
    kb::editor::tests::Require(kb::editor::ProjectFilesAssetIconResolver::Resolve(type, false).kind == kb::editor::HeroIconKind::DocumentText,
        "KBMAT-GRAPH-0005: Material Type should use a schema/document icon instead of the material preview sphere");

    const kb::editor::ProjectFilesMaterialPreviewThumbnailPolicy materialPolicy =
        kb::editor::ProjectFilesMaterialPreviewThumbnailPolicy::Resolve(material);
    const kb::editor::ProjectFilesMaterialPreviewThumbnailPolicy instancePolicy =
        kb::editor::ProjectFilesMaterialPreviewThumbnailPolicy::Resolve(instance);
    const kb::editor::ProjectFilesMaterialPreviewThumbnailPolicy meshPolicy =
        kb::editor::ProjectFilesMaterialPreviewThumbnailPolicy::Resolve(mesh);
    const kb::editor::ProjectFilesMaterialPreviewThumbnailPolicy graphPolicy =
        kb::editor::ProjectFilesMaterialPreviewThumbnailPolicy::Resolve(graph);
    const kb::editor::ProjectFilesMaterialPreviewThumbnailPolicy typePolicy =
        kb::editor::ProjectFilesMaterialPreviewThumbnailPolicy::Resolve(type);
    kb::editor::tests::Require(materialPolicy.usesPreviewScenePrimitive && materialPolicy.primitiveName == "sphere" &&
            materialPolicy.vertexCount > 0U && materialPolicy.triangleCount > 0U && materialPolicy.boundsRadius > 0.0F,
        "KBMAT-UE-0007: Material thumbnails should use the real preview sphere primitive policy");
    kb::editor::tests::Require(instancePolicy.usesPreviewScenePrimitive && instancePolicy.primitiveName == "sphere",
        "KBMAT-UE-0007: Material instance thumbnails should use the material preview primitive policy");
    kb::editor::tests::Require(!meshPolicy.usesPreviewScenePrimitive,
        "KBMAT-UE-0007: Non-material assets should not use the material preview primitive thumbnail policy");
    kb::editor::tests::Require(!graphPolicy.usesPreviewScenePrimitive && !typePolicy.usesPreviewScenePrimitive,
        "KBMAT-GRAPH-0005: Raw Material Graph and Material Type assets should use metadata icons, not material preview thumbnails");
}

void RunMaterialThumbnailPreviewRuntimeModelTest() {
    ResetTempRoot();
    const std::filesystem::path materialPath = TempRoot() / "ThumbnailProbe.kbmat";

    WritePreviewMaterial(materialPath, 0.10F, 0.62F, 0.90F);
    const kb::assets::AssetMetadata blueMaterial = MaterialMetadata("ThumbnailProbe", materialPath, 1U);
    const kb::editor::ProjectFilesMaterialPreviewStyle blueStyle =
        kb::editor::ProjectFilesMaterialPreviewThumbnailModel::StyleFromAsset(blueMaterial);
    kb::editor::tests::Require(blueStyle.loadedFromAsset && !blueStyle.errorFallback, "KBMAT-UE-0015: Valid material thumbnail style should load from the .kbmat document");
    const kb::editor::ProjectFilesMaterialPreviewImage blueImage =
        kb::editor::ProjectFilesMaterialPreviewThumbnailModel::RenderImage(64, 64, blueStyle, false);
    kb::editor::tests::Require(HasVisiblePreviewPixels(blueImage), "KBMAT-UE-0015: Material thumbnail should render non-empty preview pixels");

    WritePreviewMaterial(materialPath, 0.88F, 0.20F, 0.12F);
    const kb::assets::AssetMetadata redMaterial = MaterialMetadata("ThumbnailProbe", materialPath, 2U);
    const kb::editor::ProjectFilesMaterialPreviewStyle redStyle =
        kb::editor::ProjectFilesMaterialPreviewThumbnailModel::StyleFromAsset(redMaterial);
    kb::editor::tests::Require(redStyle.loadedFromAsset && !redStyle.errorFallback, "KBMAT-UE-0015: Material thumbnail should reload after the saved asset changes");
    const kb::editor::ProjectFilesMaterialPreviewImage redImage =
        kb::editor::ProjectFilesMaterialPreviewThumbnailModel::RenderImage(64, 64, redStyle, false);
    kb::editor::tests::Require(redImage.bgra != blueImage.bgra, "KBMAT-UE-0015: Material thumbnail pixels should update after save/content hash change");

    WriteTextFile(materialPath, "materialType builtin.pbr\nbaseColor nope\n");
    const kb::assets::AssetMetadata invalidMaterial = MaterialMetadata("BrokenThumbnailProbe", materialPath, 3U);
    const kb::editor::ProjectFilesMaterialPreviewStyle errorStyle =
        kb::editor::ProjectFilesMaterialPreviewThumbnailModel::StyleFromAsset(invalidMaterial);
    kb::editor::tests::Require(errorStyle.errorFallback && !errorStyle.loadedFromAsset, "KBMAT-UE-0015: Invalid material thumbnail should use the visible error material policy");
    const kb::editor::ProjectFilesMaterialPreviewImage errorImage =
        kb::editor::ProjectFilesMaterialPreviewThumbnailModel::RenderImage(64, 64, errorStyle, false);
    kb::editor::tests::Require(HasVisiblePreviewPixels(errorImage), "KBMAT-UE-0015: Error material thumbnail should still render visible preview pixels");
    kb::editor::tests::Require(CountErrorTintPixels(errorImage) >= 48U, "KBMAT-UE-0015: Error material thumbnail should expose a visible magenta/red diagnostic tint");
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
    RunDependencySafetyBlocksRenameAndDeleteTest();
    RunSearchTextShortcutStateTest();
    RunImportCommandReturnsMaterialTextureReportTest();
    RunImportCommandPublishesSkeletalGltfTest();
    RunImportCommandPublishesCombinedSkeletalMeshMaterialsAndTexturesTest();
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
    RunMaterialThumbnailPreviewRuntimeModelTest();
#endif
}

} // namespace kb::editor::tests
