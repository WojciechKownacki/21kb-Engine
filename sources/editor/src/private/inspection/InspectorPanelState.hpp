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
    AudioSource,
    AudioListener,
    Asset,
    Details,
    Folder,
    InputAction,
    InputMappings,
    Material,
    MaterialPreview,
    Script,
    AddComponent,
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
    ComponentMenuButton,
    ScrollbarTrack,
    ScrollbarThumb,
    ValueTypeOption,
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
    InputActionName,
    InputActionValueType,
    InputActionConsume,
    InputMappingKey,
    InputMappingAction,
    InputMappingScale,
    InputMappingTrigger,
    InputMappingRemove,
    InputMappingAdd,
    MeshRendererMesh,
    MeshRendererMaterial,
    MeshRendererMaterialSlot0,
    MeshRendererMaterialSlot1,
    MeshRendererMaterialSlot2,
    MeshRendererMaterialSlot3,
    MeshRendererMaterialSlot4,
    MeshRendererMaterialSlot5,
    MeshRendererMaterialSlot6,
    MeshRendererMaterialSlot7,
    MaterialBaseColorR,
    MaterialBaseColorG,
    MaterialBaseColorB,
    MaterialBaseColorA,
    MaterialMetallicFactor,
    MaterialRoughnessFactor,
    MaterialNormalScale,
    MaterialOcclusionStrength,
    MaterialEmissiveColorR,
    MaterialEmissiveColorG,
    MaterialEmissiveColorB,
    MaterialEmissiveStrength,
    MaterialAlphaCutoff,
    MaterialAlphaMode,
    MaterialDoubleSided,
    MaterialAlbedoTexture,
    MaterialNormalTexture,
    MaterialMetallicRoughnessTexture,
    MaterialOcclusionTexture,
    MaterialEmissiveTexture,
    ScriptName,
    ScriptEnabled,
    AudioSourceClip,
    AudioSourceVolume,
    AudioSourcePitch,
    AudioSourceEnabled,
    AudioSourceAutoplay,
    AudioSourceLoop,
    AudioSourceMute,
    AudioSourceSpatial,
    AudioSourceAttenuation,
    AudioSourceRange,
    AudioListenerEnabled,
    AudioListenerPrimary,
    ComponentRemove,
    AddComponentButton,
    AddComponentSearch,
    AddComponentOption,
};

struct InspectorPanelState {
    [[nodiscard]] bool IsCollapsed(InspectorSectionId section) const noexcept;
    void ToggleCollapsed(InspectorSectionId section) noexcept;

    [[nodiscard]] bool IsAddComponentBrowserOpen() const noexcept { return addComponentBrowserOpen_; }
    void ToggleAddComponentBrowser();
    void CloseAddComponentBrowser() noexcept;
    [[nodiscard]] bool IsComponentMenuOpen(InspectorSectionId section) const noexcept;
    void ToggleComponentMenu(InspectorSectionId section) noexcept;
    void CloseComponentMenus() noexcept;
    [[nodiscard]] bool SetHover(InspectorHitKind kind, InspectorSectionId section, InspectorPropertyId property) noexcept;
    void ClearHover() noexcept;
    [[nodiscard]] bool IsHovered(InspectorHitKind kind, InspectorSectionId section, InspectorPropertyId property) const noexcept;
    [[nodiscard]] bool IsAnyHovered() const noexcept;
    [[nodiscard]] InspectorPropertyId HoveredProperty() const noexcept;
    [[nodiscard]] bool IsListeningForKey() const noexcept;
    [[nodiscard]] int KeyCaptureMappingIndex() const noexcept;
    void BeginKeyCapture(int mappingIndex) noexcept;
    void EndKeyCapture() noexcept;
    [[nodiscard]] bool IsTextEditing() const noexcept;
    [[nodiscard]] InspectorPropertyId EditedProperty() const noexcept;
    [[nodiscard]] const std::string& EditBuffer() const noexcept;
    // Row index the current text edit targets in a dynamic list (e.g. which
    // mapping's scale is being edited). -1 when not applicable.
    [[nodiscard]] int EditIndex() const noexcept;
    void SetEditIndex(int index) noexcept;
    void BeginTextEdit(InspectorPropertyId property, std::string value);
    void AppendText(wchar_t character);
    void InsertText(std::string_view text);
    void BackspaceText();
    void ClearText() noexcept;
    void SelectAllText() noexcept;
    void EndTextEdit() noexcept;
    // Inline Value Type dropdown (Input Action / Input Axis value-type picker).
    void ToggleValueTypeDropdown() noexcept;
    void CloseValueTypeDropdown() noexcept;
    [[nodiscard]] bool IsValueTypeDropdownOpen() const noexcept;
    void SetValueTypeDropdownHover(int index) noexcept;
    [[nodiscard]] int ValueTypeDropdownHover() const noexcept;
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
    [[nodiscard]] int ScrollOffset() const noexcept { return scrollOffset_; }
    [[nodiscard]] bool SetScrollOffset(int offset, int maxOffset) noexcept;
    [[nodiscard]] bool IsScrollbarDragging() const noexcept { return scrollbarDragging_; }
    void BeginScrollbarDrag(int y) noexcept;
    void DragScrollbar(int y, int trackPixels, int maxOffset) noexcept;
    void EndScrollbarDrag() noexcept;

private:
    bool generalCollapsed_ = false;
    bool transformCollapsed_ = false;
    bool assetCollapsed_ = false;
    bool detailsCollapsed_ = false;
    bool audioSourceCollapsed_ = false;
    bool audioListenerCollapsed_ = false;
    bool meshRendererCollapsed_ = false;
    bool folderCollapsed_ = false;
    bool inputActionCollapsed_ = false;
    bool inputMappingsCollapsed_ = false;
    bool materialCollapsed_ = false;
    bool materialPreviewCollapsed_ = false;
    bool scriptCollapsed_ = false;
    bool addComponentBrowserOpen_ = false;
    bool scriptComponentMenuOpen_ = false;
    InspectorHitKind hoveredKind_ = InspectorHitKind::None;
    InspectorSectionId hoveredSection_ = InspectorSectionId::None;
    InspectorPropertyId hoveredProperty_ = InspectorPropertyId::None;
    InspectorPropertyId editedProperty_ = InspectorPropertyId::None;
    std::string editBuffer_;
    int editIndex_ = -1;
    bool valueTypeDropdownOpen_ = false;
    int valueTypeDropdownHover_ = -1;
    bool editSelectingAll_ = false;
    bool listeningForKey_ = false;
    int keyCaptureMappingIndex_ = -1;
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
    int scrollOffset_ = 0;
    bool scrollbarDragging_ = false;
    int scrollbarDragStartY_ = 0;
    int scrollbarDragStartOffset_ = 0;
};

} // namespace kb::editor
