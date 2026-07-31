#include "inspection/InspectorPanelState.hpp"

#include <algorithm>
#include <utility>

namespace kb::editor {
namespace {

constexpr float kMeshPreviewDefaultZoom = 1.0F;
constexpr float kMeshPreviewFitZoom = 1.35F;

} // namespace

bool InspectorPanelState::IsCollapsed(InspectorSectionId section) const noexcept {
    return collapsedSections_.find(section) != collapsedSections_.end();
}

void InspectorPanelState::ToggleCollapsed(InspectorSectionId section) noexcept {
    if (section == InspectorSectionId::None) {
        return;
    }
    if (const auto it = collapsedSections_.find(section); it != collapsedSections_.end()) {
        collapsedSections_.erase(it);
    } else {
        collapsedSections_.insert(section);
    }
}

bool InspectorPanelState::IsDisclosureExpanded(InspectorDisclosureId disclosure) const noexcept {
    return disclosures_[static_cast<std::size_t>(disclosure)].expanded;
}

float InspectorPanelState::DisclosureExpansion(InspectorDisclosureId disclosure) const noexcept {
    const float t = std::clamp(disclosures_[static_cast<std::size_t>(disclosure)].linearExpansion, 0.0F, 1.0F);
    return t * t * (3.0F - 2.0F * t);
}

void InspectorPanelState::ToggleDisclosure(InspectorDisclosureId disclosure) noexcept {
    DisclosureState& state = disclosures_[static_cast<std::size_t>(disclosure)];
    state.expanded = !state.expanded;
}

bool InspectorPanelState::TickDisclosures(float deltaSeconds) noexcept {
    constexpr float kTransitionSeconds = 0.18F;
    const float step = std::max(0.0F, deltaSeconds) / kTransitionSeconds;
    bool changed = false;
    for (DisclosureState& state : disclosures_) {
        const float target = state.expanded ? 1.0F : 0.0F;
        const float previous = state.linearExpansion;
        if (state.linearExpansion < target) {
            state.linearExpansion = std::min(target, state.linearExpansion + step);
        } else if (state.linearExpansion > target) {
            state.linearExpansion = std::max(target, state.linearExpansion - step);
        }
        changed = changed || state.linearExpansion != previous;
    }
    return changed;
}

void InspectorPanelState::ToggleAddComponentBrowser() {
    scriptComponentMenuOpen_ = false;
    addComponentBrowserOpen_ = !addComponentBrowserOpen_;
    // Always (re)open at the top-level category list, unscrolled and settled.
    addComponentView_ = AddComponentView::Categories;
    addComponentCategory_.clear();
    addComponentScroll_ = 0;
    addComponentSlide_ = 1.0F;
    addComponentScrollDragging_ = false;
    if (addComponentBrowserOpen_) {
        BeginTextEdit(InspectorPropertyId::AddComponentSearch, {});
    } else if (editedProperty_ == InspectorPropertyId::AddComponentSearch) {
        EndTextEdit();
    }
}

void InspectorPanelState::CloseAddComponentBrowser() noexcept {
    addComponentBrowserOpen_ = false;
    addComponentView_ = AddComponentView::Categories;
    addComponentCategory_.clear();
    addComponentScroll_ = 0;
    addComponentSlide_ = 1.0F;
    addComponentScrollDragging_ = false;
    if (editedProperty_ == InspectorPropertyId::AddComponentSearch) {
        EndTextEdit();
    }
}

void InspectorPanelState::OpenAddComponentCategory(std::string category) noexcept {
    addComponentView_ = AddComponentView::Components;
    addComponentCategory_ = std::move(category);
    addComponentScroll_ = 0;
    addComponentSlide_ = 0.0F; // start the slide-in animation
    addComponentSlideForward_ = true;
    addComponentScrollDragging_ = false;
}

void InspectorPanelState::CloseAddComponentCategory() noexcept {
    addComponentView_ = AddComponentView::Categories;
    addComponentCategory_.clear();
    addComponentScroll_ = 0;
    addComponentSlide_ = 0.0F;
    addComponentSlideForward_ = false;
    addComponentScrollDragging_ = false;
}

void InspectorPanelState::SetAddComponentScroll(int offset, int maxScroll) noexcept {
    addComponentScroll_ = std::clamp(offset, 0, std::max(0, maxScroll));
}

bool InspectorPanelState::TickAddComponentSlide(float deltaSeconds) noexcept {
    if (addComponentSlide_ >= 1.0F) {
        return false;
    }
    // ~0.18s slide; clamp to keep it snappy even on a slow frame.
    addComponentSlide_ = std::min(1.0F, addComponentSlide_ + std::max(0.0F, deltaSeconds) / 0.18F);
    return addComponentSlide_ < 1.0F;
}

void InspectorPanelState::BeginAddComponentScrollbarDrag(int grabOffset) noexcept {
    addComponentScrollDragging_ = true;
    addComponentScrollGrab_ = grabOffset;
}

void InspectorPanelState::EndAddComponentScrollbarDrag() noexcept {
    addComponentScrollDragging_ = false;
}

bool InspectorPanelState::SetHover(InspectorHitKind kind, InspectorSectionId section, InspectorPropertyId property, int index) noexcept {
    if (hoveredKind_ == kind && hoveredSection_ == section && hoveredProperty_ == property && hoveredIndex_ == index) {
        return false;
    }
    hoveredKind_ = kind;
    hoveredSection_ = section;
    hoveredProperty_ = property;
    hoveredIndex_ = index;
    return true;
}

bool InspectorPanelState::IsComponentMenuOpen(InspectorSectionId section) const noexcept {
    return section == InspectorSectionId::Script && scriptComponentMenuOpen_;
}

void InspectorPanelState::ToggleComponentMenu(InspectorSectionId section) noexcept {
    CloseAddComponentBrowser();
    if (section == InspectorSectionId::Script) {
        scriptComponentMenuOpen_ = !scriptComponentMenuOpen_;
    }
}

void InspectorPanelState::CloseComponentMenus() noexcept {
    scriptComponentMenuOpen_ = false;
}

void InspectorPanelState::ClearHover() noexcept {
    static_cast<void>(SetHover(InspectorHitKind::None, InspectorSectionId::None, InspectorPropertyId::None));
}

void InspectorPanelState::ToggleValueTypeDropdown() noexcept {
    valueTypeDropdownOpen_ = !valueTypeDropdownOpen_;
    valueTypeDropdownHover_ = -1;
}

void InspectorPanelState::CloseValueTypeDropdown() noexcept {
    valueTypeDropdownOpen_ = false;
    valueTypeDropdownHover_ = -1;
}

bool InspectorPanelState::IsValueTypeDropdownOpen() const noexcept {
    return valueTypeDropdownOpen_;
}

void InspectorPanelState::SetValueTypeDropdownHover(int index) noexcept {
    valueTypeDropdownHover_ = index;
}

int InspectorPanelState::ValueTypeDropdownHover() const noexcept {
    return valueTypeDropdownHover_;
}

void InspectorPanelState::ToggleTagsDropdown() noexcept {
    tagsDropdownOpen_ = !tagsDropdownOpen_;
    tagsDropdownHover_ = -1;
}

void InspectorPanelState::CloseTagsDropdown() noexcept {
    tagsDropdownOpen_ = false;
    tagsDropdownHover_ = -1;
}

bool InspectorPanelState::IsTagsDropdownOpen() const noexcept {
    return tagsDropdownOpen_;
}

void InspectorPanelState::SetTagsDropdownHover(int index) noexcept {
    tagsDropdownHover_ = index;
}

int InspectorPanelState::TagsDropdownHover() const noexcept {
    return tagsDropdownHover_;
}

bool InspectorPanelState::IsHovered(InspectorHitKind kind, InspectorSectionId section, InspectorPropertyId property, int index) const noexcept {
    return hoveredKind_ == kind && hoveredSection_ == section && hoveredProperty_ == property && (index < 0 || hoveredIndex_ == index);
}

bool InspectorPanelState::IsAnyHovered() const noexcept {
    return hoveredKind_ != InspectorHitKind::None;
}

InspectorPropertyId InspectorPanelState::HoveredProperty() const noexcept {
    return hoveredProperty_;
}

int InspectorPanelState::HoveredIndex() const noexcept {
    return hoveredIndex_;
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

bool InspectorPanelState::IsTextEditDirty() const noexcept {
    return IsTextEditing() && editBuffer_ != editOriginalBuffer_;
}

InspectorPropertyId InspectorPanelState::EditedProperty() const noexcept {
    return editedProperty_;
}

const std::string& InspectorPanelState::EditBuffer() const noexcept {
    return editBuffer_;
}

int InspectorPanelState::EditIndex() const noexcept {
    return editIndex_;
}

void InspectorPanelState::SetEditIndex(int index) noexcept {
    editIndex_ = index;
}

void InspectorPanelState::BeginTextEdit(InspectorPropertyId property, std::string value) {
    editedProperty_ = property;
    editOriginalBuffer_ = value;
    editBuffer_ = std::move(value);
    editIndex_ = -1;
    editSelectingAll_ = false;
    EndFloatDrag();
}

void InspectorPanelState::AppendText(wchar_t character) {
    if (character < 32 || character == 127 || (character >= 0xD800 && character <= 0xDFFF)) {
        return;
    }
    if (editSelectingAll_) {
        editBuffer_.clear();
        editSelectingAll_ = false;
    }
    const std::uint32_t codePoint = static_cast<std::uint32_t>(character);
    if (codePoint <= 0x7FU) {
        editBuffer_.push_back(static_cast<char>(codePoint));
    } else if (codePoint <= 0x7FFU) {
        editBuffer_.push_back(static_cast<char>(0xC0U | (codePoint >> 6U)));
        editBuffer_.push_back(static_cast<char>(0x80U | (codePoint & 0x3FU)));
    } else {
        editBuffer_.push_back(static_cast<char>(0xE0U | (codePoint >> 12U)));
        editBuffer_.push_back(static_cast<char>(0x80U | ((codePoint >> 6U) & 0x3FU)));
        editBuffer_.push_back(static_cast<char>(0x80U | (codePoint & 0x3FU)));
    }
}

void InspectorPanelState::InsertText(std::string_view text) {
    if (editSelectingAll_) {
        editBuffer_.clear();
        editSelectingAll_ = false;
    }
    for (const char character : text) {
        const unsigned char byte = static_cast<unsigned char>(character);
        if (byte >= 32U && byte != 127U) {
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
    editOriginalBuffer_.clear();
    editBuffer_.clear();
    editIndex_ = -1;
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

bool InspectorPanelState::SetScrollOffset(int offset, int maxOffset) noexcept {
    const int clamped = std::clamp(offset, 0, std::max(0, maxOffset));
    if (scrollOffset_ == clamped) {
        return false;
    }
    scrollOffset_ = clamped;
    return true;
}

void InspectorPanelState::BeginScrollbarDrag(int y) noexcept {
    scrollbarDragging_ = true;
    scrollbarDragStartY_ = y;
    scrollbarDragStartOffset_ = scrollOffset_;
}

void InspectorPanelState::DragScrollbar(int y, int trackPixels, int maxOffset) noexcept {
    if (!scrollbarDragging_) {
        return;
    }
    const int delta = y - scrollbarDragStartY_;
    const int offsetDelta = trackPixels <= 0 ? 0 : (delta * std::max(0, maxOffset)) / trackPixels;
    static_cast<void>(SetScrollOffset(scrollbarDragStartOffset_ + offsetDelta, maxOffset));
}

void InspectorPanelState::EndScrollbarDrag() noexcept {
    scrollbarDragging_ = false;
}

} // namespace kb::editor
