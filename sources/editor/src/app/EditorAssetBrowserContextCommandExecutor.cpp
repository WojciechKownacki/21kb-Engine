#include "app/EditorAssetBrowserContextCommandExecutor.hpp"

#include "assets/EditorAssetBrowserState.hpp"
#include "engine/scene/LightComponent.hpp"
#include "engine/scene/SceneAssets.hpp"
#include "platform/win32/EditorAssetImportDialog.hpp"
#include "scene/EditorSceneContext.hpp"

#include <filesystem>

namespace kb::editor {

bool EditorAssetBrowserContextCommandExecutor::Execute(EditorAssetContextCommand command, EditorSceneContext& sceneContext) {
    EditorAssetBrowserState& state = sceneContext.AssetBrowser();
    kb::assets::AssetManager& manager = sceneContext.Scene().Assets().Manager();
    const EditorAssetContextTargetKind targetKind = state.ContextMenuTargetKind();
    const kb::assets::AssetId targetAsset = state.ContextMenuTargetAsset();
    const std::filesystem::path targetFolder = state.ContextMenuTargetFolder();

    switch (command) {
    case EditorAssetContextCommand::Open:
        if (targetKind == EditorAssetContextTargetKind::Asset) {
            return sceneContext.OpenMaterialEditorAsset(targetAsset);
        }
        return false;
    case EditorAssetContextCommand::Import: {
        const std::filesystem::path destinationFolder = targetKind == EditorAssetContextTargetKind::Folder ? targetFolder : state.SelectedFolder();
        const std::vector<std::filesystem::path> files = EditorAssetImportDialog::Open(GetActiveWindow());
        return files.empty() ? true : sceneContext.ImportAssetFiles(files, destinationFolder);
    }
    case EditorAssetContextCommand::NewFolder:
        if (targetKind == EditorAssetContextTargetKind::Folder) {
            static_cast<void>(state.SelectFolder(targetFolder, manager));
        }
        return sceneContext.BeginAssetFolderCreation();
    case EditorAssetContextCommand::NewLuaScript: {
        const std::filesystem::path destinationFolder = targetKind == EditorAssetContextTargetKind::Folder ? targetFolder : state.SelectedFolder();
        return sceneContext.CreateLuaScriptAsset(destinationFolder);
    }
    case EditorAssetContextCommand::NewMaterial: {
        const std::filesystem::path destinationFolder = targetKind == EditorAssetContextTargetKind::Folder ? targetFolder : state.SelectedFolder();
        return sceneContext.CreateMaterialAsset(destinationFolder);
    }
    case EditorAssetContextCommand::NewMaterialGraph: {
        const std::filesystem::path destinationFolder = targetKind == EditorAssetContextTargetKind::Folder ? targetFolder : state.SelectedFolder();
        return sceneContext.CreateMaterialGraphAsset(destinationFolder);
    }
    case EditorAssetContextCommand::NewMaterialType: {
        const std::filesystem::path destinationFolder = targetKind == EditorAssetContextTargetKind::Folder ? targetFolder : state.SelectedFolder();
        return sceneContext.CreateMaterialTypeAsset(destinationFolder);
    }
    case EditorAssetContextCommand::Duplicate:
        if (targetKind == EditorAssetContextTargetKind::Asset) {
            return sceneContext.DuplicateMaterialAsset(targetAsset);
        }
        return false;
    case EditorAssetContextCommand::CreateMaterialInstance:
        if (targetKind == EditorAssetContextTargetKind::Asset) {
            return sceneContext.CreateMaterialInstanceAsset(targetAsset);
        }
        return false;
    case EditorAssetContextCommand::CreateMaterialFromGraph:
        if (targetKind == EditorAssetContextTargetKind::Asset) {
            return sceneContext.CreateMaterialFromGraphAsset(targetAsset);
        }
        return false;
    case EditorAssetContextCommand::CreateMaterialFromMaterialType:
        if (targetKind == EditorAssetContextTargetKind::Asset) {
            return sceneContext.CreateMaterialFromMaterialTypeAsset(targetAsset);
        }
        return false;
    case EditorAssetContextCommand::NewInputAction: {
        const std::filesystem::path destinationFolder = targetKind == EditorAssetContextTargetKind::Folder ? targetFolder : state.SelectedFolder();
        return sceneContext.CreateInputActionAsset(destinationFolder);
    }
    case EditorAssetContextCommand::NewInputAxis: {
        const std::filesystem::path destinationFolder = targetKind == EditorAssetContextTargetKind::Folder ? targetFolder : state.SelectedFolder();
        return sceneContext.CreateInputAxisAsset(destinationFolder);
    }
    case EditorAssetContextCommand::NewInputMappingContext: {
        const std::filesystem::path destinationFolder = targetKind == EditorAssetContextTargetKind::Folder ? targetFolder : state.SelectedFolder();
        return sceneContext.CreateInputMappingContextAsset(destinationFolder);
    }
    case EditorAssetContextCommand::ExtractMaterials:
        if (targetKind == EditorAssetContextTargetKind::Asset) {
            return sceneContext.ExtractEmbeddedMaterials(targetAsset);
        }
        return false;
    case EditorAssetContextCommand::AddDirectionalLight:
        return sceneContext.CreateLightObject(kb::scene::LightKind::Directional).IsValid();
    case EditorAssetContextCommand::AddPointLight:
        return sceneContext.CreateLightObject(kb::scene::LightKind::Point).IsValid();
    case EditorAssetContextCommand::AddSpotLight:
        return sceneContext.CreateLightObject(kb::scene::LightKind::Spot).IsValid();
    case EditorAssetContextCommand::Rename:
        if (targetKind == EditorAssetContextTargetKind::Asset) {
            return sceneContext.BeginAssetRename(targetAsset);
        }
        if (targetKind == EditorAssetContextTargetKind::Folder) {
            return sceneContext.BeginAssetFolderRename(targetFolder);
        }
        return sceneContext.BeginAssetRename();
    case EditorAssetContextCommand::Delete:
        if (targetKind == EditorAssetContextTargetKind::Asset) {
            return sceneContext.DeleteAssetBrowserItem(targetAsset);
        }
        if (targetKind == EditorAssetContextTargetKind::Folder) {
            return sceneContext.DeleteAssetBrowserFolder(targetFolder);
        }
        return sceneContext.DeleteSelectedAssetBrowserItem();
    case EditorAssetContextCommand::FindReferences:
        if (targetKind == EditorAssetContextTargetKind::Asset) {
            return sceneContext.FindMaterialReferences(targetAsset);
        }
        return false;
    case EditorAssetContextCommand::Refresh:
        static_cast<void>(sceneContext.Scene().Assets().Discover());
        return true;
    case EditorAssetContextCommand::None:
    default:
        return false;
    }
}

} // namespace kb::editor
