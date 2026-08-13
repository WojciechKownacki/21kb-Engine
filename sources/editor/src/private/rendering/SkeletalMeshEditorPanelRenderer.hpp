#pragma once

#include "kb/editor/docking/DockTypes.hpp"
#include "kb/editor/theme/EditorTheme.hpp"
#include "rendering/EditorRenderBackendSettings.hpp"
#include "rendering/EditorSceneBgfxViewport.hpp"
#include "scene/EditorSceneContext.hpp"

#include <optional>
#include <cstdint>
#include <string>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif

namespace kb::editor {

enum class SkeletalAssetDocument : std::uint8_t {
    Mesh,
    Skeleton,
};

enum class SkeletalAssetCommand : std::uint8_t {
    Save,
    Undo,
    Redo,
    Reload,
    PreviewMesh,
    AddSocket,
    DuplicateSocket,
    DeleteSocket,
    BoundsMode,
    ReferencePose,
    Focus,
};

enum class SkeletalMeshEditorDetailsHitKind : std::uint8_t {
    Section,
    Field,
};

struct SkeletalMeshEditorDetailsHit {
    SkeletalMeshEditorDetailsHitKind kind = SkeletalMeshEditorDetailsHitKind::Section;
    std::string sectionTitle;
    SkeletalMeshEditorDetailsField field{};
};

class SkeletalMeshEditorPanelRenderer {
public:
    static constexpr int TreeRowHeight = 20;
#if defined(_WIN32)
    void Paint(
        HDC dc,
        HWND host,
        const RECT& content,
        const DockPanel& panel,
        const EditorTheme& theme,
        const EditorSceneContext& sceneContext,
        const EditorRenderBackendSettings& renderBackendSettings,
        EditorSceneBgfxViewport* sceneViewport) const;
    [[nodiscard]] static bool PresentViewport(
        EditorSceneBgfxViewport& sceneViewport,
        HWND host,
        const RECT& content,
        const DockPanel& panel,
        const EditorSceneContext& sceneContext,
        const EditorRenderBackendSettings& renderBackendSettings);
    [[nodiscard]] static std::optional<SkeletalMeshEditorTreeRow> TreeRowAt(
        const RECT& content, const EditorSceneContext& sceneContext, int x, int y);
    [[nodiscard]] static std::optional<kb::scene::SkeletonBoneId> TreeDisclosureAt(
        const RECT& content, const EditorSceneContext& sceneContext, int x, int y);
    [[nodiscard]] static RECT TreeListRect(
        const RECT& content, const EditorSceneContext& sceneContext) noexcept;
    [[nodiscard]] static int TreeMaxScroll(
        const RECT& content, const EditorSceneContext& sceneContext);
    [[nodiscard]] static RECT TreeScrollbarTrack(
        const RECT& content, const EditorSceneContext& sceneContext) noexcept;
    [[nodiscard]] static RECT TreeScrollbarThumb(
        const RECT& content, const EditorSceneContext& sceneContext);
    [[nodiscard]] static int TreeScrollOffsetToRevealSelection(
        const RECT& content, const EditorSceneContext& sceneContext);
    [[nodiscard]] static bool IsTreeSearchAt(
        const RECT& content, const EditorSceneContext& sceneContext, int x, int y) noexcept;
    [[nodiscard]] static std::optional<std::uint8_t> AdvancedPreviewOverlayAt(
        const RECT& content, const EditorSceneContext& sceneContext, int x, int y) noexcept;
    [[nodiscard]] static std::optional<SkeletalAssetDocument> LinkedDocumentAt(
        const RECT& content, const EditorSceneContext& sceneContext, int x, int y) noexcept;
    [[nodiscard]] static std::optional<SkeletalAssetCommand> CommandAt(
        const RECT& content, const EditorSceneContext& sceneContext, int x, int y);
    [[nodiscard]] static std::optional<kb::scene::SkeletonBoneId> BoneAt(
        const RECT& content, const EditorSceneContext& sceneContext, int x, int y) noexcept;
    [[nodiscard]] static std::optional<SkeletalMeshEditorDetailsHit> DetailsHitAt(
        const RECT& content, const EditorSceneContext& sceneContext, int x, int y);
    [[nodiscard]] static RECT DetailsListRect(
        const RECT& content, const EditorSceneContext& sceneContext) noexcept;
    [[nodiscard]] static int DetailsMaxScroll(
        const RECT& content, const EditorSceneContext& sceneContext);
    [[nodiscard]] static RECT DetailsScrollbarTrack(
        const RECT& content, const EditorSceneContext& sceneContext) noexcept;
    [[nodiscard]] static RECT DetailsScrollbarThumb(
        const RECT& content, const EditorSceneContext& sceneContext);
#endif
};

} // namespace kb::editor
