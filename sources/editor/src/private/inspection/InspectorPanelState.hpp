#pragma once

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
};

} // namespace kb::editor
