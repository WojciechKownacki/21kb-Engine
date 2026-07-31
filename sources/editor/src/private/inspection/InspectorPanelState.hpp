#pragma once

#include "rendering/EditorMeshPreviewTypes.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <set>
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
    Animator,
    UIDocument,
    NavAgent,
    NavObstacle,
    Tags,
    RegionShape,
    GuideCurve,
    ContentInstance,
    StreamFocus,
    WorldBackdrop,
    AmbientRadiance,
    DetailSwitch,
    VisibilityBlocker,
    VisibilityCell,
    RegionPortal,
    Asset,
    Details,
    Folder,
    InputAction,
    InputMappings,
    Material,
    MaterialPreview,
    Script,
    Rigidbody,
    Collider,
    CharacterController,
    Joint,
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

// Animated disclosure groups are shared Inspector UI state. Add a value here
// when another component needs a collapsible subcategory; rendering, easing and
// frame ticking remain common.
enum class InspectorDisclosureId : std::uint8_t {
    MeshRendererAdvanced,
    Count,
};

enum class InspectorPropertyId : std::uint8_t {
    None,
    EntityName,
    EntityVisible,
    EntityVisibilityMode,
    EntityVisibilityMask,
    PositionX,
    PositionY,
    PositionZ,
    RotationX,
    RotationY,
    RotationZ,
    ScaleX,
    ScaleY,
    ScaleZ,
    CameraProjection,
    CameraVerticalFov,
    CameraOrthographicHeight,
    CameraNearClip,
    CameraFarClip,
    CameraPrimary,
    CameraViewportId,
    CameraPriority,
    CameraCullingMask,
    CameraClearMode,
    CameraClearColorR,
    CameraClearColorG,
    CameraClearColorB,
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
    MeshRendererMeshPicker,
    MeshRendererMaterial,
    MeshRendererMaterialPicker,
    MeshRendererMaterialOverridePicker,
    MeshRendererAdvanced,
    MeshRendererCastsShadow,
    MeshRendererReceivesShadow,
    MeshRendererMaterialSlot0,
    MeshRendererMaterialSlot1,
    MeshRendererMaterialSlot2,
    MeshRendererMaterialSlot3,
    MeshRendererMaterialSlot4,
    MeshRendererMaterialSlot5,
    MeshRendererMaterialSlot6,
    MeshRendererMaterialSlot7,
    MeshRendererMaterialSlotPicker0,
    MeshRendererMaterialSlotPicker1,
    MeshRendererMaterialSlotPicker2,
    MeshRendererMaterialSlotPicker3,
    MeshRendererMaterialSlotPicker4,
    MeshRendererMaterialSlotPicker5,
    MeshRendererMaterialSlotPicker6,
    MeshRendererMaterialSlotPicker7,
    LightKind,
    LightColorR,
    LightColorG,
    LightColorB,
    LightIntensity,
    LightRange,
    LightInnerCone,
    LightOuterCone,
    LightAreaWidth,
    LightAreaHeight,
    LightContactShadowLength,
    LightVolumetricScattering,
    LightCastsShadow,
    LightUseColorTemperature,
    LightColorTemperatureKelvin,
    LightLayerMask,
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
    // The Script field's picker ("magnifier") button — reveals the bound Lua
    // asset in Project Files, mirroring the Mesh/Material asset-field pickers.
    ScriptPicker,
    ScriptEnabled,
    // One row per exposed ("@expose") script variable; the specific variable is
    // identified by the Hit's/edit's row index (like InputMapping entries).
    ScriptVariable,
    // One row per physics-component field, keyed by row index (the "one id +
    // index" model as ScriptVariable). One id per component so an in-flight float
    // edit is unambiguous even when an entity carries several physics components.
    RigidbodyField,
    ColliderField,
    CharacterControllerField,
    JointField,
    // The Collider section's "Fit to Mesh" action button.
    ColliderFitToMesh,
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
    AnimatorController,
    AnimatorSpeed,
    AnimatorEnabled,
    AnimatorRootMotionOwner,
    UIDocumentAsset,
    UIDocumentEnabled,
    NavAgentRadius,
    NavAgentHeight,
    NavAgentMaxSpeed,
    NavAgentAcceleration,
    NavAgentAngularSpeed,
    NavAgentStoppingDistance,
    NavAgentAreaMask,
    NavAgentDestinationX,
    NavAgentDestinationY,
    NavAgentDestinationZ,
    NavAgentEnabled,
    NavObstacleShape,
    NavObstacleCenterX,
    NavObstacleCenterY,
    NavObstacleCenterZ,
    NavObstacleSizeX,
    NavObstacleSizeY,
    NavObstacleSizeZ,
    NavObstacleRadius,
    NavObstacleHeight,
    NavObstacleArea,
    NavObstacleCarve,
    NavObstacleEnabled,
    TagsText,
    RegionShapeKind,
    RegionShapeCenterX,
    RegionShapeCenterY,
    RegionShapeCenterZ,
    RegionShapeSizeX,
    RegionShapeSizeY,
    RegionShapeSizeZ,
    RegionShapeRadius,
    RegionShapeHeight,
    RegionShapeEnabled,
    GuideCurveControlPointCount,
    GuideCurveInterpolation,
    GuideCurveClosed,
    GuideCurveEnabled,
    ContentInstanceAssetId,
    ContentInstanceKind,
    ContentInstanceLifetime,
    ContentInstanceActive,
    StreamFocusInnerRadius,
    StreamFocusOuterRadius,
    StreamFocusPriority,
    StreamFocusLoadMask,
    StreamFocusEnabled,
    WorldBackdropMode,
    WorldBackdropColorR,
    WorldBackdropColorG,
    WorldBackdropColorB,
    WorldBackdropHorizonColorR,
    WorldBackdropHorizonColorG,
    WorldBackdropHorizonColorB,
    WorldBackdropZenithColorR,
    WorldBackdropZenithColorG,
    WorldBackdropZenithColorB,
    WorldBackdropEnvironmentAssetId,
    WorldBackdropHorizonHeight,
    WorldBackdropGradientExponent,
    WorldBackdropPriority,
    WorldBackdropEnabled,
    AmbientRadianceMode,
    AmbientRadianceColorR,
    AmbientRadianceColorG,
    AmbientRadianceColorB,
    AmbientRadianceHorizonColorR,
    AmbientRadianceHorizonColorG,
    AmbientRadianceHorizonColorB,
    AmbientRadianceZenithColorR,
    AmbientRadianceZenithColorG,
    AmbientRadianceZenithColorB,
    AmbientRadianceEnvironmentAssetId,
    AmbientRadianceIntensity,
    AmbientRadianceDiffuseIntensity,
    AmbientRadianceSpecularIntensity,
    AmbientRadiancePriority,
    AmbientRadianceEnabled,
    DetailSwitchGroupId,
    DetailSwitchMinimumLod,
    DetailSwitchMaximumLod,
    DetailSwitchPromoteCoverage,
    DetailSwitchDemoteCoverage,
    DetailSwitchEnabled,
    VisibilityBlockerCenterX,
    VisibilityBlockerCenterY,
    VisibilityBlockerCenterZ,
    VisibilityBlockerSizeX,
    VisibilityBlockerSizeY,
    VisibilityBlockerSizeZ,
    VisibilityBlockerEnabled,
    VisibilityCellMembershipMask,
    VisibilityCellMembership,
    VisibilityCellOverride,
    VisibilityCellEnabled,
    RegionPortalSourceCell,
    RegionPortalTargetCell,
    RegionPortalPurposes,
    RegionPortalEnabled,
    ComponentRemove,
    AddComponentButton,
    AddComponentSearch,
    AddComponentOption,
    AddComponentBack,
};

struct InspectorPanelState {
    [[nodiscard]] bool IsCollapsed(InspectorSectionId section) const noexcept;
    void ToggleCollapsed(InspectorSectionId section) noexcept;

    [[nodiscard]] bool IsDisclosureExpanded(InspectorDisclosureId disclosure) const noexcept;
    [[nodiscard]] float DisclosureExpansion(InspectorDisclosureId disclosure) const noexcept;
    void ToggleDisclosure(InspectorDisclosureId disclosure) noexcept;
    [[nodiscard]] bool TickDisclosures(float deltaSeconds) noexcept;

    [[nodiscard]] bool IsAddComponentBrowserOpen() const noexcept { return addComponentBrowserOpen_; }
    void ToggleAddComponentBrowser();
    void CloseAddComponentBrowser() noexcept;

    // Add Component menu navigation (Unity-style two levels: category list ->
    // that category's components, with an animated horizontal slide between them).
    enum class AddComponentView : std::uint8_t { Categories, Components };
    [[nodiscard]] AddComponentView AddComponentBrowserView() const noexcept { return addComponentView_; }
    [[nodiscard]] const std::string& AddComponentBrowserCategory() const noexcept { return addComponentCategory_; }
    // Enter a category (slide forward) / return to the category list (slide back).
    void OpenAddComponentCategory(std::string category) noexcept;
    void CloseAddComponentCategory() noexcept;
    // Scroll offset in pixels within the results list, clamped to [0, maxScroll].
    [[nodiscard]] int AddComponentScroll() const noexcept { return addComponentScroll_; }
    void SetAddComponentScroll(int offset, int maxScroll) noexcept;
    // Horizontal slide animation: 1.0 = settled, <1.0 = mid-transition. Forward
    // means "entering a category" (new content slides in from the right).
    [[nodiscard]] float AddComponentSlide() const noexcept { return addComponentSlide_; }
    [[nodiscard]] bool AddComponentSlideForward() const noexcept { return addComponentSlideForward_; }
    // Advances the slide by dt; returns true while still animating.
    [[nodiscard]] bool TickAddComponentSlide(float deltaSeconds) noexcept;
    // Draggable results scrollbar.
    void BeginAddComponentScrollbarDrag(int grabOffset) noexcept;
    void EndAddComponentScrollbarDrag() noexcept;
    [[nodiscard]] bool IsAddComponentScrollbarDragging() const noexcept { return addComponentScrollDragging_; }
    [[nodiscard]] int AddComponentScrollbarGrabOffset() const noexcept { return addComponentScrollGrab_; }
    [[nodiscard]] bool IsComponentMenuOpen(InspectorSectionId section) const noexcept;
    void ToggleComponentMenu(InspectorSectionId section) noexcept;
    void CloseComponentMenus() noexcept;
    // `index` disambiguates rows that share one property id (physics fields,
    // script variables). Pass -1 for singular rows. IsHovered/HoveredIndex report
    // it so only the row actually under the cursor highlights.
    [[nodiscard]] bool SetHover(InspectorHitKind kind, InspectorSectionId section, InspectorPropertyId property, int index = -1) noexcept;
    void ClearHover() noexcept;
    [[nodiscard]] bool IsHovered(InspectorHitKind kind, InspectorSectionId section, InspectorPropertyId property, int index = -1) const noexcept;
    [[nodiscard]] bool IsAnyHovered() const noexcept;
    [[nodiscard]] InspectorPropertyId HoveredProperty() const noexcept;
    [[nodiscard]] int HoveredIndex() const noexcept;
    [[nodiscard]] bool IsListeningForKey() const noexcept;
    [[nodiscard]] int KeyCaptureMappingIndex() const noexcept;
    void BeginKeyCapture(int mappingIndex) noexcept;
    void EndKeyCapture() noexcept;
    [[nodiscard]] bool IsTextEditing() const noexcept;
    [[nodiscard]] bool IsTextEditDirty() const noexcept;
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
    // Every section that is currently collapsed. A set (rather than one bool per
    // section) so any InspectorSectionId — including the physics component
    // sections — collapses without extending a hand-maintained switch.
    struct DisclosureState {
        bool expanded = false;
        float linearExpansion = 0.0F;
    };

    std::set<InspectorSectionId> collapsedSections_;
    std::array<DisclosureState, static_cast<std::size_t>(InspectorDisclosureId::Count)> disclosures_{};
    bool addComponentBrowserOpen_ = false;
    AddComponentView addComponentView_ = AddComponentView::Categories;
    std::string addComponentCategory_;
    int addComponentScroll_ = 0;
    float addComponentSlide_ = 1.0F;
    bool addComponentSlideForward_ = true;
    bool addComponentScrollDragging_ = false;
    int addComponentScrollGrab_ = 0;
    bool scriptComponentMenuOpen_ = false;
    InspectorHitKind hoveredKind_ = InspectorHitKind::None;
    InspectorSectionId hoveredSection_ = InspectorSectionId::None;
    InspectorPropertyId hoveredProperty_ = InspectorPropertyId::None;
    int hoveredIndex_ = -1;
    InspectorPropertyId editedProperty_ = InspectorPropertyId::None;
    std::string editOriginalBuffer_;
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
