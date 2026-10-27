#include "app/EditorAssetBrowserContextCommandExecutor.hpp"

#include "app/EditorAssetBrowserDoubleClickHandler.hpp"
#include "assets/EditorAssetBrowserState.hpp"
#include "assets/EditorAssetReimport.hpp"
#include "engine/assets/AssetImportCatalog.hpp"
#include "localization/EditorLocalization.hpp"
#include "platform/win32/EditorChoiceDialog.hpp"
#include "platform/win32/EditorFileDialogFilter.hpp"
#include "rendering/script_editor/ScriptEditorTextEncoding.hpp"
#include "engine/scene/LightComponent.hpp"
#include "engine/scene/ParticleEffectAssetIO.hpp"
#include "engine/scene/SceneAssets.hpp"
#include "platform/win32/EditorAssetImportDialog.hpp"
#include "scene/EditorSceneContext.hpp"

#include <CommDlg.h>

#include <filesystem>
#include <optional>

namespace kb::editor {
namespace {

// The project's import presets for the import dialog.
[[nodiscard]] std::vector<EditorAssetImportPreset> ImportPresetChoices(EditorSceneContext& sceneContext) {
    std::vector<EditorAssetImportPreset> presets;
    for (auto& [name, options] : sceneContext.ImportPresets()) {
        presets.push_back(EditorAssetImportPreset{ .name = std::move(name), .options = options });
    }
    return presets;
}

// The changed source of an imported model, asked from the author (the file the model was imported from is not remembered by path).
[[nodiscard]] std::optional<std::filesystem::path> ChooseReimportSource(HWND owner) {
    std::string extensions;
    for (const std::string& extension : kb::assets::AssetImportCatalog::SupportedSourceExtensions()) {
        extensions += (extensions.empty() ? "*" : ";*") + extension;
    }
    const std::wstring filter = EditorFileDialogFilter({
        { std::string{ EditorText("assets.reimport.modelFiles", "Model files") }, extensions },
        { std::string{ EditorText("fileDialog.allFiles", "All files (*.*)") }, "*.*" },
    });
    const std::wstring title = ScriptEditorTextEncoding::Widen(EditorText("assets.reimport.chooseSource", "Choose the changed source file"));
    std::wstring buffer(4096U, L'\0');
    OPENFILENAMEW openFileName{};
    openFileName.lStructSize = sizeof(openFileName);
    openFileName.hwndOwner = owner;
    openFileName.lpstrFilter = filter.c_str();
    openFileName.lpstrFile = buffer.data();
    openFileName.nMaxFile = static_cast<DWORD>(buffer.size());
    openFileName.lpstrTitle = title.c_str();
    openFileName.Flags = OFN_EXPLORER | OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (GetOpenFileNameW(&openFileName) == FALSE) return std::nullopt;
    return std::filesystem::path{ buffer.c_str() };
}

// What to do with textures and materials edited since the import, asked in the editor's own dialog.
[[nodiscard]] EditorAssetReimport::Decision AskAboutEditedAssets(HWND owner, const std::vector<kb::assets::AssetImportChange>& conflicts) {
    std::string names;
    for (std::size_t index = 0U; index < conflicts.size() && index < 4U; ++index) {
        names += (index == 0U ? "" : ", ") + conflicts[index].virtualPath.filename().string();
    }
    if (conflicts.size() > 4U) names += "...";
    switch (EditorChoiceDialog::Show(owner, EditorChoiceDialogDescriptor{
        .title = std::string{ EditorText("assets.reimport.title", "Reimport") },
        .message = EditorLocalization::Format("assets.reimport.conflict", "{count} asset(s) were edited since the last import: {names}.",
            { { "count", std::to_string(conflicts.size()) }, { "names", names } }),
        .supportingText = std::string{ EditorText("assets.reimport.conflictHelp",
            "Overwrite replaces them; the old files are kept and Undo Last Reimport restores them. Keep my edits leaves them and updates the rest.") },
        .primaryLabel = std::string{ EditorText("assets.reimport.keep", "Keep my edits") },
        .secondaryLabel = std::string{ EditorText("assets.reimport.overwrite", "Overwrite") },
        .cancelLabel = std::string{ EditorText("common.cancel", "Cancel") },
        .icon = HeroIconKind::DocumentText,
    })) {
    case EditorChoiceDialogResult::Primary: return EditorAssetReimport::Decision::KeepLocal;
    case EditorChoiceDialogResult::Secondary: return EditorAssetReimport::Decision::Overwrite;
    case EditorChoiceDialogResult::Cancel:
    default: return EditorAssetReimport::Decision::Cancel;
    }
}

} // namespace


bool EditorAssetBrowserContextCommandExecutor::Execute(EditorAssetContextCommand command, EditorSceneContext& sceneContext, HWND owner) {
    EditorAssetBrowserState& state = sceneContext.AssetBrowser();
    kb::assets::AssetManager& manager = sceneContext.Scene().Assets().Manager();
    const EditorAssetContextTargetKind targetKind = state.ContextMenuTargetKind();
    const kb::assets::AssetId targetAsset = state.ContextMenuTargetAsset();
    const std::filesystem::path targetFolder = state.ContextMenuTargetFolder();

    switch (command) {
    case EditorAssetContextCommand::Open:
        if (targetKind == EditorAssetContextTargetKind::Asset) {
            const kb::assets::AssetMetadata* metadata = manager.Registry().Find(targetAsset);
            return metadata != nullptr &&
                EditorAssetBrowserDoubleClickHandler::OpenAsset(owner, *metadata, sceneContext) !=
                    EditorAssetBrowserDoubleClickResult::None;
        }
        return false;
    case EditorAssetContextCommand::Import: {
        const std::filesystem::path destinationFolder = targetKind == EditorAssetContextTargetKind::Folder ? targetFolder : state.SelectedFolder();
        const std::optional<EditorAssetImportSelection> selection = EditorAssetImportDialog::Open(GetActiveWindow(), ImportPresetChoices(sceneContext));
        return !selection.has_value() || selection->files.empty()
            ? true
            : sceneContext.BeginAssetImport(selection->files, destinationFolder, selection->options);
    }
    case EditorAssetContextCommand::Reimport: {
        if (targetKind != EditorAssetContextTargetKind::Asset) return false;
        const kb::assets::AssetMetadata* metadata = manager.Registry().Find(targetAsset);
        if (metadata == nullptr || !EditorAssetReimport::CanReimport(*metadata)) return false;
        const std::optional<std::filesystem::path> source = ChooseReimportSource(owner);
        if (!source.has_value()) return true;
        const EditorAssetReimport::Outcome outcome = EditorAssetReimport::Run(sceneContext, targetAsset, *source, EditorAssetReimport::OptionsOf(manager, targetAsset),
            [owner](const std::vector<kb::assets::AssetImportChange>& conflicts) { return AskAboutEditedAssets(owner, conflicts); });
        return outcome.succeeded || outcome.cancelled;
    }
    case EditorAssetContextCommand::UndoReimport:
        return EditorAssetReimport::UndoLast(sceneContext);
    case EditorAssetContextCommand::NewFolder:
        if (targetKind == EditorAssetContextTargetKind::Folder) {
            static_cast<void>(state.SelectFolder(targetFolder, manager));
        }
        return sceneContext.BeginAssetFolderCreation();
    case EditorAssetContextCommand::NewLuaScript: {
        const std::filesystem::path destinationFolder = targetKind == EditorAssetContextTargetKind::Folder ? targetFolder : state.SelectedFolder();
        return sceneContext.CreateLuaScriptAsset(destinationFolder);
    }
    case EditorAssetContextCommand::NewNativeScript: {
        const std::filesystem::path destinationFolder = targetKind == EditorAssetContextTargetKind::Folder ? targetFolder : state.SelectedFolder();
        return sceneContext.CreateNativeScriptAsset(destinationFolder);
    }
    case EditorAssetContextCommand::NewMaterial: {
        const std::filesystem::path destinationFolder = targetKind == EditorAssetContextTargetKind::Folder ? targetFolder : state.SelectedFolder();
        return sceneContext.CreateMaterialAsset(destinationFolder);
    }
    case EditorAssetContextCommand::NewMaterialFunction: {
        const std::filesystem::path destinationFolder = targetKind == EditorAssetContextTargetKind::Folder ? targetFolder : state.SelectedFolder();
        return sceneContext.CreateMaterialFunctionAsset(destinationFolder);
    }
    case EditorAssetContextCommand::NewMaterialGraph: {
        const std::filesystem::path destinationFolder = targetKind == EditorAssetContextTargetKind::Folder ? targetFolder : state.SelectedFolder();
        return sceneContext.CreateMaterialGraphAsset(destinationFolder);
    }
    case EditorAssetContextCommand::NewMaterialType: {
        const std::filesystem::path destinationFolder = targetKind == EditorAssetContextTargetKind::Folder ? targetFolder : state.SelectedFolder();
        return sceneContext.CreateMaterialTypeAsset(destinationFolder);
    }
    case EditorAssetContextCommand::Duplicate: {
        if (targetKind != EditorAssetContextTargetKind::Asset) {
            return false;
        }
        // Materials keep their gateway path, which rewrites the document rather than
        // copying its bytes; everything else duplicates through the shared copy.
        const kb::assets::AssetMetadata* metadata = manager.Registry().Find(targetAsset);
        if (metadata != nullptr && metadata->type == "RenderMaterial") {
            return sceneContext.DuplicateMaterialAsset(targetAsset);
        }
        return sceneContext.DuplicateAsset(targetAsset);
    }
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
    case EditorAssetContextCommand::NewAudioMixer: {
        const std::filesystem::path destinationFolder = targetKind == EditorAssetContextTargetKind::Folder ? targetFolder : state.SelectedFolder();
        return sceneContext.CreateAudioMixerAsset(destinationFolder);
    }
    case EditorAssetContextCommand::NewPostProcessProfile: {
        const std::filesystem::path destinationFolder = targetKind == EditorAssetContextTargetKind::Folder ? targetFolder : state.SelectedFolder();
        return sceneContext.CreatePostProcessProfileAsset(destinationFolder);
    }
    case EditorAssetContextCommand::NewImportPreset: {
        const std::filesystem::path destinationFolder = targetKind == EditorAssetContextTargetKind::Folder ? targetFolder : state.SelectedFolder();
        return sceneContext.CreateImportPresetAsset(destinationFolder);
    }
    case EditorAssetContextCommand::NewParticleEffect: {
        const std::filesystem::path destinationFolder = targetKind == EditorAssetContextTargetKind::Folder ? targetFolder : state.SelectedFolder();
        return sceneContext.CreateParticleEffectAsset(destinationFolder);
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
    case EditorAssetContextCommand::FindReferences: {
        if (targetKind != EditorAssetContextTargetKind::Asset) {
            return false;
        }
        const kb::assets::AssetMetadata* metadata = manager.Registry().Find(targetAsset);
        if (metadata != nullptr && metadata->type == kb::scene::kParticleEffectAssetType) {
            return sceneContext.FindParticleEffectReferences(targetAsset);
        }
        return sceneContext.FindMaterialReferences(targetAsset);
    }
    case EditorAssetContextCommand::Refresh:
        static_cast<void>(sceneContext.Scene().Assets().Discover());
        return true;
    case EditorAssetContextCommand::None:
    default:
        return false;
    }
}

} // namespace kb::editor
