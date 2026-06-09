#pragma once

#include "rendering/EditorMeshPreviewTypes.hpp"

#include <cstdint>
#include <string>
#include <string_view>

namespace kb::editor {

enum class InspectorSectionId : std::uint8_t {
    None,
    General,
    Transform,
    Camera,
    MeshRenderer,
    Light,
    Asset,
    Folder,
};

enum class InspectorHitKind : std::uint8_t {
    None,
    Row,
    SectionHeader,
    TextField,
    BoolField,
    FloatField,
    MeshPreview,
    MeshPreviewToolbarButton,
};

enum class InspectorPropertyId : std::uint8_t {
    None,
    EntityName,
    EntityVisible,
    PositionX,
    PositionY,
    PositionZ,
    RotationX,
    RotationY,
    RotationZ,
    ScaleX,
    ScaleY,
    ScaleZ,
    MeshPreviewReset,
    MeshPreviewFit,
    MeshPreviewRenderMode,
    MeshPreviewLightPreset,
};

struct InspectorPanelState {
    [[nodiscard]] bool IsCollapsed(InspectorSectionId section) const noexcept;
    void ToggleCollapsed(InspectorSectionId section) noexcept;
    [[nodiscard]] bool SetHover(InspectorHitKind kind, InspectorSectionId section, InspectorPropertyId property) noexcept;
    void ClearHover() noexcept;
    [[nodiscard]] bool IsHovered(InspectorHitKind kind, InspectorSectionId section, InspectorPropertyId property) const noexcept;
    [[nodiscard]] bool IsAnyHovered() const noexcept;
    [[nodiscard]] InspectorPropertyId HoveredProperty() const noexcept;
    [[nodiscard]] bool IsTextEditing() const noexcept;
    [[nodiscard]] InspectorPropertyId EditedProperty() const noexcept;
    [[nodiscard]] const std::string& EditBuffer() const noexcept;
    void BeginTextEdit(InspectorPropertyId property, std::string value);
    void AppendText(wchar_t character);
    void InsertText(std::string_view text);
    void BackspaceText();
    void ClearText() noexcept;
    void SelectAllText() noexcept;
    void EndTextEdit() noexcept;
    [[nodiscard]] bool IsDraggingFloat() const noexcept;
    [[nodiscard]] InspectorPropertyId DraggedProperty() const noexcept;
    [[nodiscard]] float DragStartValue() const noexcept;
    [[nodiscard]] int DragStartX() const noexcept;
    [[nodiscard]] int DragStartY() const noexcept;
    [[nodiscard]] bool FloatDragMoved() const noexcept;
    void BeginFloatDrag(InspectorPropertyId property, float startValue, int x, int y) noexcept;
    void MarkFloatDragMoved() noexcept;
    void EndFloatDrag() noexcept;
    [[nodiscard]] bool IsDraggingMeshPreview() const noexcept;
    [[nodiscard]] float MeshPreviewYaw() const noexcept;
    [[nodiscard]] float MeshPreviewPitch() const noexcept;
    [[nodiscard]] float MeshPreviewZoom() const noexcept;
    [[nodiscard]] EditorMeshPreviewRenderMode MeshPreviewRenderMode() const noexcept;
    [[nodiscard]] EditorMeshPreviewLightPreset MeshPreviewLightPreset() const noexcept;
    [[nodiscard]] int MeshPreviewDragStartX() const noexcept;
    [[nodiscard]] int MeshPreviewDragStartY() const noexcept;
    [[nodiscard]] float MeshPreviewDragStartYaw() const noexcept;
    [[nodiscard]] float MeshPreviewDragStartPitch() const noexcept;
    void BeginMeshPreviewDrag(int x, int y) noexcept;
    void DragMeshPreview(int x, int y) noexcept;
    void EndMeshPreviewDrag() noexcept;
    [[nodiscard]] bool ZoomMeshPreview(float delta) noexcept;
    void ResetMeshPreview() noexcept;
    void FitMeshPreview() noexcept;
    void CycleMeshPreviewRenderMode() noexcept;
    void CycleMeshPreviewLightPreset() noexcept;

private:
    bool generalCollapsed_ = false;
    bool transformCollapsed_ = false;
    bool assetCollapsed_ = false;
    bool folderCollapsed_ = false;
    InspectorHitKind hoveredKind_ = InspectorHitKind::None;
    InspectorSectionId hoveredSection_ = InspectorSectionId::None;
    InspectorPropertyId hoveredProperty_ = InspectorPropertyId::None;
    InspectorPropertyId editedProperty_ = InspectorPropertyId::None;
    std::string editBuffer_;
    bool editSelectingAll_ = false;
    InspectorPropertyId draggedProperty_ = InspectorPropertyId::None;
    float dragStartValue_ = 0.0F;
    int dragStartX_ = 0;
    int dragStartY_ = 0;
    bool floatDragMoved_ = false;
    bool meshPreviewDragging_ = false;
    int meshPreviewDragStartX_ = 0;
    int meshPreviewDragStartY_ = 0;
    float meshPreviewYaw_ = -35.0F;
    float meshPreviewPitch_ = 24.0F;
    float meshPreviewZoom_ = 1.0F;
    EditorMeshPreviewRenderMode meshPreviewRenderMode_ = EditorMeshPreviewRenderMode::Solid;
    EditorMeshPreviewLightPreset meshPreviewLightPreset_ = EditorMeshPreviewLightPreset::Studio;
    float meshPreviewDragStartYaw_ = -35.0F;
    float meshPreviewDragStartPitch_ = 24.0F;
};

} // namespace kb::editor
