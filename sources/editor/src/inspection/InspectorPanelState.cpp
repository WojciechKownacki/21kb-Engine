#include "inspection/InspectorPanelState.hpp"

#include <utility>

namespace kb::editor {

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
    EndFloatDrag();
}

void InspectorPanelState::AppendText(wchar_t character) {
    if (character >= 32 && character <= 126) {
        editBuffer_.push_back(static_cast<char>(character));
    }
}

void InspectorPanelState::BackspaceText() {
    if (!editBuffer_.empty()) {
        editBuffer_.pop_back();
    }
}

void InspectorPanelState::EndTextEdit() noexcept {
    editedProperty_ = InspectorPropertyId::None;
    editBuffer_.clear();
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

} // namespace kb::editor
