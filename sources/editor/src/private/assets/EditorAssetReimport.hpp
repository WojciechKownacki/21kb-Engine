#pragma once

#include "engine/assets/AssetId.hpp"
#include "engine/assets/AssetImportRecord.hpp"
#include "engine/assets/AssetImportTypes.hpp"
#include "engine/assets/AssetManager.hpp"
#include "engine/assets/AssetMetadata.hpp"

#include <filesystem>
#include <functional>
#include <vector>

namespace kb::editor {

class EditorSceneContext;

// Reimport of an imported model from the Project Files panel and from automation: the changed source replaces the model in place, and the
// textures and materials the last import wrote are not overwritten when they were edited since, unless the author says so. What a reimport
// overwrote can be put back (Undo Last Reimport). See docs/asset_reimport.md.
class EditorAssetReimport final {
public:
    EditorAssetReimport() = delete;

    enum class Decision : std::uint8_t { Overwrite, KeepLocal, Cancel };
    // Asked once, when the reimport would overwrite assets edited since the last import.
    using ConflictPrompt = std::function<Decision(const std::vector<kb::assets::AssetImportChange>& conflicts)>;

    struct Outcome {
        bool succeeded = false;
        bool cancelled = false;
        kb::assets::AssetImportItemResult item{};
    };

    [[nodiscard]] static bool CanReimport(const kb::assets::AssetMetadata& metadata) noexcept;
    // The settings of the last import of `id` (its record), for a reimport that changes none of them.
    [[nodiscard]] static kb::assets::AssetImportOptions OptionsOf(const kb::assets::AssetManager& manager, kb::assets::AssetId id);
    // `options.conflictPolicy` is where the reimport starts: Report asks `prompt` (when there is one) about edited assets; KeepLocal and
    // Overwrite decide in advance. The result is logged to the console.
    [[nodiscard]] static Outcome Run(EditorSceneContext& context, kb::assets::AssetId id, const std::filesystem::path& source,
        kb::assets::AssetImportOptions options, const ConflictPrompt& prompt = {});
    [[nodiscard]] static bool CanUndo(const kb::assets::AssetManager& manager);
    // Restores the files of the newest backup (the last reimport that overwrote anything).
    [[nodiscard]] static bool UndoLast(EditorSceneContext& context);
};

// Whether the menu offers them: needs no scene, so the menu state and the tests can use them without the rest of the editor.
inline bool EditorAssetReimport::CanReimport(const kb::assets::AssetMetadata& metadata) noexcept {
    return !metadata.physicalPath.empty() && (metadata.type == "SkeletalMesh" ||
        (metadata.type == "RenderMesh" && metadata.importCategory == "Mesh"));
}

inline bool EditorAssetReimport::CanUndo(const kb::assets::AssetManager& manager) {
    return !kb::assets::AssetImportBackups::List(manager).empty();
}

} // namespace kb::editor
