#pragma once

#include "scene/EditorViewportCameraState.hpp"

#include "engine/assets/AssetImportTypes.hpp"
#include "engine/assets/AssetManager.hpp"
#include "engine/assets/AssetMetadata.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneEntity.hpp"

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace kb::render {
struct RenderMeshAssetData;
}

namespace kb::editor {

enum class StaticMeshViewMode : std::uint8_t {
    Lit,
    Normals,
};

struct StaticMeshMaterialSlot {
    std::string name;
    std::string material;
};

// What the Mesh and Materials cards and the LOD ruler show, read from the loaded mesh.
struct StaticMeshSummary {
    // Triangles of every level, finest first.
    std::vector<std::uint64_t> levelTriangles;
    std::uint64_t vertices = 0U;
    std::uint32_t sections = 0U;
    std::array<float, 3> size{};
    std::vector<StaticMeshMaterialSlot> materialSlots;
};

// The collapsed cards and the scroll position of the side panel.
struct StaticMeshEditorUiState {
    bool meshCardOpen = true;
    bool materialsCardOpen = true;
    bool lodCardOpen = true;
    bool advancedOpen = false;
    int scroll = 0;
    // The slider being dragged: the panel's action and the level it belongs to; 0 when none is.
    std::uint8_t dragAction = 0U;
    std::uint32_t dragLevel = 0U;
};

// The state of the Static Mesh window: the mesh being inspected, the level-of-detail settings being edited, the
// chain built from them and the studio the chain is previewed in. One mesh is open at a time. Loading the mesh
// and building its levels run on a worker; Tick() takes a finished build and the window repaints from it.
class StaticMeshEditorSession {
public:
    StaticMeshEditorSession();
    ~StaticMeshEditorSession();
    StaticMeshEditorSession(const StaticMeshEditorSession&) = delete;
    StaticMeshEditorSession& operator=(const StaticMeshEditorSession&) = delete;

    [[nodiscard]] static StaticMeshEditorSession& Instance();
    // Whether the Static Mesh window opens `metadata`.
    [[nodiscard]] static bool CanOpen(const kb::assets::AssetMetadata& metadata) noexcept;

    // Opens the mesh: reads its settings and starts loading it and building its levels. A mesh that is not an
    // imported model opens read-only, with Problem() saying so.
    [[nodiscard]] bool Open(const kb::scene::Scene& source, const kb::assets::AssetMetadata& metadata);
    // Stops the worker without waiting for it and drops everything the window held.
    void Close() noexcept;
    [[nodiscard]] bool IsOpen() const noexcept { return asset_.id.IsValid(); }
    // True once after Open(): the shell then shows the window.
    [[nodiscard]] bool ConsumeOpenRequest() noexcept;
    // Takes a finished build and advances the camera; true when the window needs a repaint.
    [[nodiscard]] bool Tick(float deltaSeconds);

    [[nodiscard]] const kb::assets::AssetMetadata& Asset() const noexcept { return asset_; }
    [[nodiscard]] std::string Title() const;
    // The settings can be edited and applied: the mesh is an imported model.
    [[nodiscard]] bool Editable() const noexcept { return editable_; }
    // Why the mesh is read-only, could not be loaded or could not be built; empty when all is well.
    [[nodiscard]] const std::string& Problem() const noexcept { return problem_; }
    [[nodiscard]] bool Building() const noexcept { return job_ != nullptr; }
    [[nodiscard]] float BuildSeconds() const noexcept { return buildSeconds_; }

    [[nodiscard]] const kb::assets::AssetMeshLodSettings& Draft() const noexcept { return draft_; }
    [[nodiscard]] kb::assets::AssetMeshLodSettings& Draft() noexcept { return draft_; }
    [[nodiscard]] const kb::assets::AssetMeshLodSettings& Applied() const noexcept { return applied_; }
    // The draft differs from what the asset stores.
    [[nodiscard]] bool Modified() const noexcept { return draft_ != applied_; }
    // A new level in Manual mode keeps half of what the level above keeps, as an Auto level would.
    void SetLevelCount(std::uint8_t count) noexcept;
    // Switches the draft between Auto and Manual. Manual starts from the levels Auto builds now, so no level moves.
    void SetManual(bool manual) noexcept;
    // Why `lod` cannot be stored; empty when it can.
    [[nodiscard]] static std::string Validate(const kb::assets::AssetMeshLodSettings& lod);
    // Stores the draft in the asset and rebuilds the levels from it. Returns why it did not; empty on success.
    [[nodiscard]] std::string Generate(kb::assets::AssetManager& manager);
    // The last reason Generate() gave, until the draft changes.
    [[nodiscard]] const std::string& Notice() const noexcept { return notice_; }
    void ClearNotice() noexcept { notice_.clear(); }

    [[nodiscard]] const StaticMeshSummary& Summary() const noexcept { return summary_; }
    // The level the preview is pinned to; none draws by distance.
    [[nodiscard]] std::optional<std::uint32_t> Pinned() const noexcept { return pinned_; }
    void SetPinned(std::optional<std::uint32_t> level) noexcept;
    [[nodiscard]] StaticMeshViewMode ViewMode() const noexcept { return viewMode_; }
    void SetViewMode(StaticMeshViewMode mode) noexcept { viewMode_ = mode; }
    [[nodiscard]] StaticMeshEditorUiState& Ui() noexcept { return ui_; }
    [[nodiscard]] const StaticMeshEditorUiState& Ui() const noexcept { return ui_; }

    [[nodiscard]] EditorViewportCameraState& Camera() noexcept { return camera_; }
    [[nodiscard]] const EditorViewportCameraState& Camera() const noexcept { return camera_; }
    // Frames the mesh in view.
    void Frame() noexcept;
    [[nodiscard]] const kb::scene::Scene* PreviewScene() const noexcept { return preview_.get(); }
    // Grows with every change of what the preview draws, never with a camera move.
    [[nodiscard]] std::uint64_t PreviewRevision() const noexcept { return revision_; }

private:
    struct Job;

    void StartBuild(std::optional<kb::assets::AssetMeshLodSettings> settings, bool reuseSource);
    void RetireJob() noexcept;
    void AdoptFinishedJob();
    void RebuildPreview();
    void ApplyPin() noexcept;

    kb::assets::AssetMetadata asset_{};
    std::vector<kb::assets::AssetMetadata> registry_;
    bool editable_ = false;
    bool openRequested_ = false;
    std::string problem_;
    std::string notice_;
    kb::assets::AssetMeshLodSettings draft_{};
    kb::assets::AssetMeshLodSettings applied_{};
    StaticMeshSummary summary_{};
    std::shared_ptr<const kb::render::RenderMeshAssetData> source_;
    std::shared_ptr<const kb::render::RenderMeshAssetData> built_;
    std::unique_ptr<Job> job_;
    std::vector<std::unique_ptr<Job>> retired_;
    float buildSeconds_ = 0.0F;
    float pulseSeconds_ = 0.0F;

    std::optional<std::uint32_t> pinned_{};
    StaticMeshViewMode viewMode_ = StaticMeshViewMode::Lit;
    StaticMeshEditorUiState ui_{};
    EditorViewportCameraState camera_{};
    std::unique_ptr<kb::scene::Scene> preview_;
    kb::scene::SceneEntity meshEntity_{};
    kb::scene::Vec3 focusCenter_{};
    float focusRadius_ = 1.0F;
    std::uint64_t revision_ = 1U;
};

} // namespace kb::editor
