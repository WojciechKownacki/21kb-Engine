#include "assets/EditorAssetReimport.hpp"

#include "console/EditorConsoleState.hpp"
#include "engine/assets/AssetImportRecord.hpp"
#include "engine/assets/AssetManager.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneAssets.hpp"
#include "kb/render/resources/RenderMeshImportArtifacts.hpp"
#include "scene/EditorSceneContext.hpp"

#include <algorithm>
#include <string>

namespace kb::editor {
namespace {

[[nodiscard]] std::string NamesOf(const std::vector<kb::assets::AssetImportChange>& changes) {
    std::string names;
    for (std::size_t index = 0U; index < changes.size() && index < 4U; ++index) {
        names += (index == 0U ? "" : ", ") + changes[index].virtualPath.generic_string();
    }
    if (changes.size() > 4U) names += " and " + std::to_string(changes.size() - 4U) + " more";
    return names;
}

} // namespace

kb::assets::AssetImportOptions EditorAssetReimport::OptionsOf(const kb::assets::AssetManager& manager, kb::assets::AssetId id) {
    kb::assets::AssetImportOptions options{};
    const kb::assets::AssetMetadata* metadata = manager.Registry().Find(id);
    if (metadata == nullptr) return options;
    const std::optional<kb::assets::AssetImportRecord> record = kb::assets::AssetImportRecordIO::Read(kb::assets::AssetImportRecordIO::PathFor(metadata->physicalPath));
    if (!record.has_value()) return options;
    options.mesh.importMaterialSlots = (record->optionFlags & kb::assets::kAssetImportOptionMeshDisableMaterialSlots) == 0U;
    options.mesh.importTextures = (record->optionFlags & kb::assets::kAssetImportOptionMeshImportTextures) != 0U;
    options.mesh.importMaterials = (record->optionFlags & kb::assets::kAssetImportOptionMeshImportMaterials) != 0U;
    options.mesh.combineMeshes = (record->optionFlags & kb::assets::kAssetImportOptionMeshCombineMeshes) != 0U;
    options.mesh.transform = record->transform;
    return options;
}

EditorAssetReimport::Outcome EditorAssetReimport::Run(EditorSceneContext& context, kb::assets::AssetId id, const std::filesystem::path& source,
    kb::assets::AssetImportOptions options, const ConflictPrompt& prompt) {
    Outcome outcome;
    kb::assets::AssetManager& manager = context.Scene().Assets().Manager();
    EditorConsoleState& console = context.Console();
    const std::string label = source.filename().string();
    kb::assets::AssetImportItemResult item = kb::render::RenderMeshImportArtifacts::ReimportStatic(manager, id, source, options);
    const bool edited = item.status == kb::assets::AssetImportItemStatus::Failed && !item.conflicts.empty() && !options.dryRun &&
        options.conflictPolicy == kb::assets::AssetImportConflictPolicy::Report;
    if (edited && prompt) {
        const Decision decision = prompt(item.conflicts);
        if (decision == Decision::Cancel) {
            outcome.cancelled = true;
            outcome.item = std::move(item);
            console.Warning("Assets", "Reimport of " + label + " cancelled: " + std::to_string(outcome.item.conflicts.size()) + " asset(s) were edited since the last import (" +
                NamesOf(outcome.item.conflicts) + "). Nothing was changed.");
            return outcome;
        }
        options.conflictPolicy = decision == Decision::Overwrite ? kb::assets::AssetImportConflictPolicy::Overwrite : kb::assets::AssetImportConflictPolicy::KeepLocal;
        item = kb::render::RenderMeshImportArtifacts::ReimportStatic(manager, id, source, options);
    }
    static_cast<void>(context.Scene().Assets().Discover());
    outcome.item = std::move(item);
    const kb::assets::AssetImportItemResult& result = outcome.item;
    if (result.status == kb::assets::AssetImportItemStatus::Failed || result.status == kb::assets::AssetImportItemStatus::Missing || !result.error.empty()) {
        // Refusing to overwrite edits is the policy at work, not a failure of the import.
        if (result.conflicts.empty()) console.Error("Assets", "Reimport of " + label + " failed: " + result.error);
        else console.Warning("Assets", "Reimport of " + label + " changed nothing: " + result.error);
        return outcome;
    }
    outcome.succeeded = true;
    std::size_t changed = 0U;
    for (const kb::assets::AssetImportChange& change : result.changes) {
        changed += change.kind == kb::assets::AssetImportChangeKind::Changed || change.kind == kb::assets::AssetImportChangeKind::Added ||
            change.kind == kb::assets::AssetImportChangeKind::LocalOverride ? 1U : 0U;
    }
    std::string text = std::string{ options.dryRun ? "Reimport of " : "Reimported " } + label + " into " + result.virtualPath.generic_string() + ": " +
        std::string{ kb::assets::ToString(result.status) } + ", " + std::to_string(changed) + " asset(s) to write";
    if (!result.conflicts.empty()) {
        text += options.conflictPolicy == kb::assets::AssetImportConflictPolicy::Overwrite ? "; replaced edited " : "; kept edited ";
        text += NamesOf(result.conflicts);
    }
    if (!result.backupId.empty()) text += ". Backup " + result.backupId + " (Undo Last Reimport restores the old files)";
    console.Info("Assets", text);
    return outcome;
}

bool EditorAssetReimport::UndoLast(EditorSceneContext& context) {
    kb::assets::AssetManager& manager = context.Scene().Assets().Manager();
    const std::vector<std::string> backups = kb::assets::AssetImportBackups::List(manager);
    if (backups.empty()) {
        context.Console().Warning("Assets", "There is no reimport to undo.");
        return false;
    }
    std::string error;
    if (!kb::assets::AssetImportBackups::Restore(manager, backups.back(), &error)) {
        context.Console().Error("Assets", "Undo of reimport " + backups.back() + " failed: " + error);
        return false;
    }
    static_cast<void>(context.Scene().Assets().Discover());
    context.Console().Info("Assets", "Restored the files of reimport " + backups.back() + ".");
    return true;
}

} // namespace kb::editor
