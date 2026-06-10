#include "inspection/InspectorPanelState.hpp"

#include <algorithm>
#include <utility>

namespace kb::editor {
namespace {

constexpr float kMeshPreviewDefaultZoom = 1.0F;
constexpr float kMeshPreviewFitZoom = 1.35F;

} // namespace

bool InspectorPanelState::IsCollapsed(InspectorSectionId section) const noexcept {
    switch (section) {
    case InspectorSectionId::General:
        return generalCollapsed_;
    case InspectorSectionId::Transform:
        return transformCollapsed_;
    case InspectorSectionId::Asset:
        return assetCollapsed_;
    case InspectorSectionId::Folder:
        return folderCollapsed_;
    case InspectorSectionId::InputAction:
        return inputActionCollapsed_;
    case InspectorSectionId::InputMappings:
        return inputMappingsCollapsed_;
    case InspectorSectionId::Input:
        return inputCollapsed_;
    default:
        return false;
    }
}

void InspectorPanelState::ToggleCollapsed(InspectorSectionId section) noexcept {
    bool* target = nullptr;
    switch (section) {
    case InspectorSectionId::General:
        target = &generalCollapsed_;
        break;
    case InspectorSectionId::Transform:
        target = &transformCollapsed_;
        break;
    case InspectorSectionId::Asset:
        target = &assetCollapsed_;
        break;
    case InspectorSectionId::Folder:
        target = &folderCollapsed_;
        break;
    case InspectorSectionId::InputAction:
        target = &inputActionCollapsed_;
        break;
    case InspectorSectionId::InputMappings:
        target = &inputMappingsCollapsed_;
        break;
    case InspectorSectionId::Input:
        target = &inputCollapsed_;
        break;
    default:
        break;
    }
    if (target != nullptr) {
        *target = !*target;
    }
}

bool InspectorPanelState::SetHover(InspectorHitKind kind, InspectorSectionId section, InspectorPropertyId property) noexcept {
    if (hoveredKind_ == kind && hoveredSection_ == section && hoveredProperty_ == property) {
        return false;
    }
    hoveredKind_ = kind;
    hoveredSection_ = section;
    hoveredProperty_ = property;
    return true;
}

void InspectorPanelState::ClearHover() noexcept {
    static_cast<void>(SetHover(InspectorHitKind::None, InspectorSectionId::None, InspectorPropertyId::None));
}

bool InspectorPanelState::IsHovered(InspectorHitKind kind, InspectorSectionId section, InspectorPropertyId property) const noexcept {
    return hoveredKind_ == kind && hoveredSection_ == section && hoveredProperty_ == property;
}

bool InspectorPanelState::IsAnyHovered() const noexcept {
    return hoveredKind_ != InspectorHitKind::None;
}

InspectorPropertyId InspectorPanelState::HoveredProperty() const noexcept {
    return hoveredProperty_;
}

bool InspectorPanelState::IsListeningForKey() const noexcept {
    return listeningForKey_;
}

int InspectorPanelState::KeyCaptureMappingIndex() const noexcept {
    return keyCaptureMappingIndex_;
}

void InspectorPanelState::BeginKeyCapture(int mappingIndex) noexcept {
    listeningForKey_ = true;
    keyCaptureMappingIndex_ = mappingIndex;
    EndTextEdit();
}

void InspectorPanelState::EndKeyCapture() noexcept {
    listeningForKey_ = false;
    keyCaptureMappingIndex_ = -1;
}

bool InspectorPanelState::IsTextEditing() const noexcept {
    return editedProperty_ != InspectorPropertyId::None;
}

InspectorPropertyId InspectorPanelState::EditedProperty() const noexcept {
    return editedProperty_;
}

const std::string& InspectorPanelState::EditBuffer() const noexcept {
    return editBuffer_;
}

void InspectorPanelState::BeginTextEdit(InspectorPropertyId property, std::string value) {
    editedProperty_ = property;
    editBuffer_ = std::move(value);
    editSelectingAll_ = false;
    EndFloatDrag();
}

void InspectorPanelState::AppendText(wchar_t character) {
    if (character >= 32 && character <= 126) {
        if (editSelectingAll_) {
            editBuffer_.clear();
            editSelectingAll_ = false;
        }
        editBuffer_.push_back(static_cast<char>(character));
    }
}

void InspectorPanelState::InsertText(std::string_view text) {
    if (editSelectingAll_) {
        editBuffer_.clear();
        editSelectingAll_ = false;
    }
    for (const char character : text) {
        if (character >= 32 && character <= 126) {
            editBuffer_.push_back(character);
        }
    }
}

void InspectorPanelState::BackspaceText() {
    if (editSelectingAll_) {
        ClearText();
        return;
    }
    if (!editBuffer_.empty()) {
        editBuffer_.pop_back();
    }
}

void InspectorPanelState::ClearText() noexcept {
    editBuffer_.clear();
    editSelectingAll_ = false;
}

void InspectorPanelState::SelectAllText() noexcept {
    if (IsTextEditing()) {
        editSelectingAll_ = true;
    }
}

void InspectorPanelState::EndTextEdit() noexcept {
    editedProperty_ = InspectorPropertyId::None;
    editBuffer_.clear();
    editSelectingAll_ = false;
}

bool InspectorPanelState::IsDraggingFloat() const noexcept {
    return draggedProperty_ != InspectorPropertyId::None;
}

InspectorPropertyId InspectorPanelState::DraggedProperty() const noexcept {
    return draggedProperty_;
}

float InspectorPanelState::DragStartValue() const noexcept {
    return dragStartValue_;
}

int InspectorPanelState::DragStartX() const noexcept {
    return dragStartX_;
}

int InspectorPanelState::DragStartY() const noexcept {
    return dragStartY_;
}

bool InspectorPanelState::FloatDragMoved() const noexcept {
    return floatDragMoved_;
}

void InspectorPanelState::BeginFloatDrag(InspectorPropertyId property, float startValue, int x, int y) noexcept {
    draggedProperty_ = property;
    dragStartValue_ = startValue;
    dragStartX_ = x;
    dragStartY_ = y;
    floatDragMoved_ = false;
    EndTextEdit();
}

void InspectorPanelState::MarkFloatDragMoved() noexcept {
    floatDragMoved_ = true;
}

void InspectorPanelState::EndFloatDrag() noexcept {
    draggedProperty_ = InspectorPropertyId::None;
    dragStartValue_ = 0.0F;
    dragStartX_ = 0;
    dragStartY_ = 0;
    floatDragMoved_ = false;
}

bool InspectorPanelState::IsDraggingMeshPreview() const noexcept {
    return meshPreviewDragging_;
}

float InspectorPanelState::MeshPreviewYaw() const noexcept {
    return meshPreviewYaw_;
}

float InspectorPanelState::MeshPreviewPitch() const noexcept {
    return meshPreviewPitch_;
}

float InspectorPanelState::MeshPreviewZoom() const noexcept {
    return meshPreviewZoom_;
}

EditorMeshPreviewRenderMode InspectorPanelState::MeshPreviewRenderMode() const noexcept {
    return meshPreviewRenderMode_;
}

EditorMeshPreviewLightPreset InspectorPanelState::MeshPreviewLightPreset() const noexcept {
    return meshPreviewLightPreset_;
}

int InspectorPanelState::MeshPreviewDragStartX() const noexcept {
    return meshPreviewDragStartX_;
}

int InspectorPanelState::MeshPreviewDragStartY() const noexcept {
    return meshPreviewDragStartY_;
}

float InspectorPanelState::MeshPreviewDragStartYaw() const noexcept {
    return meshPreviewDragStartYaw_;
}

float InspectorPanelState::MeshPreviewDragStartPitch() const noexcept {
    return meshPreviewDragStartPitch_;
}

void InspectorPanelState::BeginMeshPreviewDrag(int x, int y) noexcept {
    EndTextEdit();
    EndFloatDrag();
    meshPreviewDragging_ = true;
    meshPreviewDragStartX_ = x;
    meshPreviewDragStartY_ = y;
    meshPreviewDragStartYaw_ = meshPreviewYaw_;
    meshPreviewDragStartPitch_ = meshPreviewPitch_;
}

void InspectorPanelState::DragMeshPreview(int x, int y) noexcept {
    if (!meshPreviewDragging_) {
        return;
    }
    meshPreviewYaw_ = meshPreviewDragStartYaw_ + static_cast<float>(x - meshPreviewDragStartX_) * 0.38F;
    meshPreviewPitch_ = std::clamp(meshPreviewDragStartPitch_ + static_cast<float>(y - meshPreviewDragStartY_) * 0.28F, -80.0F, 80.0F);
}

void InspectorPanelState::EndMeshPreviewDrag() noexcept {
    meshPreviewDragging_ = false;
    meshPreviewDragStartX_ = 0;
    meshPreviewDragStartY_ = 0;
    meshPreviewDragStartYaw_ = meshPreviewYaw_;
    meshPreviewDragStartPitch_ = meshPreviewPitch_;
}

bool InspectorPanelState::ZoomMeshPreview(float delta) noexcept {
    const float previous = meshPreviewZoom_;
    meshPreviewZoom_ = std::clamp(meshPreviewZoom_ * (1.0F + delta), 0.55F, 2.4F);
    return previous != meshPreviewZoom_;
}

void InspectorPanelState::ResetMeshPreview() noexcept {
    meshPreviewYaw_ = -35.0F;
    meshPreviewPitch_ = 24.0F;
    meshPreviewZoom_ = kMeshPreviewDefaultZoom;
    meshPreviewRenderMode_ = EditorMeshPreviewRenderMode::Solid;
    meshPreviewLightPreset_ = EditorMeshPreviewLightPreset::Studio;
    EndMeshPreviewDrag();
}

void InspectorPanelState::FitMeshPreview() noexcept {
    meshPreviewZoom_ = kMeshPreviewFitZoom;
    EndMeshPreviewDrag();
}

void InspectorPanelState::CycleMeshPreviewRenderMode() noexcept {
    switch (meshPreviewRenderMode_) {
    case EditorMeshPreviewRenderMode::Solid:
        meshPreviewRenderMode_ = EditorMeshPreviewRenderMode::WireframeOnly;
        break;
    case EditorMeshPreviewRenderMode::WireframeOnly:
        meshPreviewRenderMode_ = EditorMeshPreviewRenderMode::WireframeOverlay;
        break;
    case EditorMeshPreviewRenderMode::WireframeOverlay:
        meshPreviewRenderMode_ = EditorMeshPreviewRenderMode::Normals;
        break;
    case EditorMeshPreviewRenderMode::Normals:
        meshPreviewRenderMode_ = EditorMeshPreviewRenderMode::Bounds;
        break;
    case EditorMeshPreviewRenderMode::Bounds:
        meshPreviewRenderMode_ = EditorMeshPreviewRenderMode::Solid;
        break;
    }
}

void InspectorPanelState::CycleMeshPreviewLightPreset() noexcept {
    switch (meshPreviewLightPreset_) {
    case EditorMeshPreviewLightPreset::Studio:
        meshPreviewLightPreset_ = EditorMeshPreviewLightPreset::Front;
        break;
    case EditorMeshPreviewLightPreset::Front:
        meshPreviewLightPreset_ = EditorMeshPreviewLightPreset::Rim;
        break;
    case EditorMeshPreviewLightPreset::Rim:
        meshPreviewLightPreset_ = EditorMeshPreviewLightPreset::Studio;
        break;
    }
}

} // namespace kb::editor
