#include "inspection/InspectorPanelInteraction.hpp"

#if defined(_WIN32)
#include "app/EditorTextInputShortcuts.hpp"
#include "assets/EditorAssetBrowserState.hpp"
#include "engine/assets/AssetId.hpp"
#include "engine/scene/SceneEntities.hpp"
#include "engine/scene/SceneTransforms.hpp"
#include "engine/scene/VisibilityComponent.hpp"
#include "inspection/InspectorInputInteraction.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cstdio>
#include <cstdlib>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace kb::editor {
namespace {

[[nodiscard]] float StepFor(InspectorPropertyId property) noexcept {
    switch (property) {
    case InspectorPropertyId::RotationX:
    case InspectorPropertyId::RotationY:
    case InspectorPropertyId::RotationZ:
        return 0.05F;
    case InspectorPropertyId::ScaleX:
    case InspectorPropertyId::ScaleY:
    case InspectorPropertyId::ScaleZ:
        return 0.1F;
    default:
        return 0.1F;
    }
}

[[nodiscard]] std::string_view Trim(std::string_view text) noexcept {
    while (!text.empty() && (text.front() == ' ' || text.front() == '\t')) {
        text.remove_prefix(1);
    }
    while (!text.empty() && (text.back() == ' ' || text.back() == '\t')) {
        text.remove_suffix(1);
    }
    return text;
}

[[nodiscard]] bool ParseFloat(std::string_view text, float& value) noexcept {
    text = Trim(text);
    const char* first = text.data();
    const char* last = text.data() + text.size();
    const std::from_chars_result result = std::from_chars(first, last, value);
    return result.ec == std::errc{} && result.ptr == last;
}

[[nodiscard]] bool EvaluateMath(std::string_view text, float currentValue, float& output) noexcept {
    text = Trim(text);
    if (text.empty()) {
        return false;
    }

    float value = 0.0F;
    std::size_t index = 0;
    if (text.front() == '+' || text.front() == '-' || text.front() == '*' || text.front() == '/') {
        value = currentValue;
    } else {
        std::size_t op = text.find_first_of("+-*/", 1);
        const std::string_view first = op == std::string_view::npos ? text : text.substr(0, op);
        if (!ParseFloat(first, value)) {
            return false;
        }
        index = op == std::string_view::npos ? text.size() : op;
    }

    while (index < text.size()) {
        const char operation = text[index++];
        while (index < text.size() && text[index] == ' ') {
            ++index;
        }
        const std::size_t next = text.find_first_of("+-*/", index);
        const std::string_view term = next == std::string_view::npos ? text.substr(index) : text.substr(index, next - index);
        float rhs = 0.0F;
        if (!ParseFloat(term, rhs)) {
            return false;
        }
        switch (operation) {
        case '+':
            value += rhs;
            break;
        case '-':
            value -= rhs;
            break;
        case '*':
            value *= rhs;
            break;
        case '/':
            if (rhs == 0.0F) {
                return false;
            }
            value /= rhs;
            break;
        default:
            return false;
        }
        index = next == std::string_view::npos ? text.size() : next;
    }

    output = value;
    return true;
}

[[nodiscard]] std::string FormatCompactFloat(float value) {
    char buffer[32]{};
    std::snprintf(buffer, sizeof(buffer), "%.3f", static_cast<double>(value));
    std::string text = buffer;
    if (text.find('.') != std::string::npos) {
        while (!text.empty() && text.back() == '0') {
            text.pop_back();
        }
        if (!text.empty() && text.back() == '.') {
            text.pop_back();
        }
    }
    return text == "-0" ? "0" : text;
}

[[nodiscard]] float ReadTransformValue(const kb::scene::TransformComponent& transform, InspectorPropertyId property) noexcept {
    switch (property) {
    case InspectorPropertyId::PositionX:
        return transform.localPosition.x;
    case InspectorPropertyId::PositionY:
        return transform.localPosition.y;
    case InspectorPropertyId::PositionZ:
        return transform.localPosition.z;
    case InspectorPropertyId::RotationX:
        return transform.localRotation.x;
    case InspectorPropertyId::RotationY:
        return transform.localRotation.y;
    case InspectorPropertyId::RotationZ:
        return transform.localRotation.z;
    case InspectorPropertyId::ScaleX:
        return transform.localScale.x;
    case InspectorPropertyId::ScaleY:
        return transform.localScale.y;
    case InspectorPropertyId::ScaleZ:
        return transform.localScale.z;
    default:
        return 0.0F;
    }
}

void SetTransformValue(kb::scene::Scene& scene, kb::scene::SceneEntity entity, InspectorPropertyId property, float value) {
    kb::scene::TransformComponent transform = scene.Transforms().Get(entity);
    switch (property) {
    case InspectorPropertyId::PositionX:
        transform.localPosition.x = value;
        break;
    case InspectorPropertyId::PositionY:
        transform.localPosition.y = value;
        break;
    case InspectorPropertyId::PositionZ:
        transform.localPosition.z = value;
        break;
    case InspectorPropertyId::RotationX:
        transform.localRotation.x = value;
        break;
    case InspectorPropertyId::RotationY:
        transform.localRotation.y = value;
        break;
    case InspectorPropertyId::RotationZ:
        transform.localRotation.z = value;
        break;
    case InspectorPropertyId::ScaleX:
        transform.localScale.x = std::max(0.01F, value);
        break;
    case InspectorPropertyId::ScaleY:
        transform.localScale.y = std::max(0.01F, value);
        break;
    case InspectorPropertyId::ScaleZ:
        transform.localScale.z = std::max(0.01F, value);
        break;
    default:
        return;
    }
    scene.Transforms().Set(entity, transform);
}

[[nodiscard]] bool IsTransformProperty(InspectorPropertyId property) noexcept {
    return property == InspectorPropertyId::PositionX || property == InspectorPropertyId::PositionY || property == InspectorPropertyId::PositionZ ||
        property == InspectorPropertyId::RotationX || property == InspectorPropertyId::RotationY || property == InspectorPropertyId::RotationZ ||
        property == InspectorPropertyId::ScaleX || property == InspectorPropertyId::ScaleY || property == InspectorPropertyId::ScaleZ;
}

} // namespace

bool InspectorPanelInteraction::HandlePointerDown(EditorSceneContext& sceneContext, const InspectorPanelRenderer::Hit& hit, int x, int y) noexcept {
    if (hit.kind == InspectorHitKind::None) {
        sceneContext.Inspector().EndTextEdit();
        return false;
    }

    kb::scene::SceneEntity entity = sceneContext.SelectedEntity();
    if (hit.kind == InspectorHitKind::SectionHeader) {
        sceneContext.Inspector().EndTextEdit();
        sceneContext.Inspector().ToggleCollapsed(hit.section);
        return true;
    }
    if (hit.kind == InspectorHitKind::MeshPreviewToolbarButton) {
        sceneContext.Inspector().EndTextEdit();
        if (hit.property == InspectorPropertyId::MeshPreviewReset) {
            sceneContext.Inspector().ResetMeshPreview();
        } else if (hit.property == InspectorPropertyId::MeshPreviewFit) {
            sceneContext.Inspector().FitMeshPreview();
        } else if (hit.property == InspectorPropertyId::MeshPreviewRenderMode) {
            sceneContext.Inspector().CycleMeshPreviewRenderMode();
        } else if (hit.property == InspectorPropertyId::MeshPreviewLightPreset) {
            sceneContext.Inspector().CycleMeshPreviewLightPreset();
        }
        return true;
    }
    if (hit.kind == InspectorHitKind::MeshPreview) {
        sceneContext.Inspector().BeginMeshPreviewDrag(x, y);
        return true;
    }
    const bool assetSelected = sceneContext.AssetBrowser().SelectionKind() == EditorAssetBrowserSelectionKind::Asset;
    if (assetSelected && hit.section == InspectorSectionId::InputAction) {
        return InspectorInputInteraction::HandleActionAssetClick(sceneContext, hit);
    }
    if (assetSelected && hit.section == InspectorSectionId::InputMappings) {
        return InspectorInputInteraction::HandleMappingClick(sceneContext, hit);
    }
    if (!sceneContext.Scene().Entities().IsAlive(entity)) {
        return true;
    }

    if (hit.kind == InspectorHitKind::TextField && hit.property == InspectorPropertyId::EntityName) {
        sceneContext.Inspector().BeginTextEdit(InspectorPropertyId::EntityName, sceneContext.Scene().Entities().Name(entity));
        return true;
    }
    if (hit.kind == InspectorHitKind::BoolField) {
        sceneContext.Inspector().EndTextEdit();
        if (hit.property == InspectorPropertyId::EntityVisible) {
            static_cast<void>(sceneContext.ToggleEntityVisibility(entity));
        }
        return true;
    }
    if (hit.kind == InspectorHitKind::FloatField) {
        const kb::scene::TransformComponent transform = sceneContext.Scene().Transforms().Get(entity);
        if (!sceneContext.BeginSelectedTransformEdit("Edit Transform")) {
            return true;
        }
        sceneContext.Inspector().BeginFloatDrag(hit.property, ReadTransformValue(transform, hit.property), x, y);
        return true;
    }
    if (hit.section == InspectorSectionId::Input) {
        return InspectorInputInteraction::HandleComponentClick(sceneContext, entity, hit);
    }
    return true;
}

bool InspectorPanelInteraction::HandlePointerDrag(EditorSceneContext& sceneContext, int x, int y) noexcept {
    if (sceneContext.Inspector().IsDraggingMeshPreview()) {
        sceneContext.Inspector().DragMeshPreview(x, y);
        return true;
    }
    if (!sceneContext.Inspector().IsDraggingFloat()) {
        return false;
    }
    const kb::scene::SceneEntity entity = sceneContext.SelectedEntity();
    if (!sceneContext.Scene().Entities().IsAlive(entity)) {
        sceneContext.CancelActiveTransformEdit();
        sceneContext.Inspector().EndFloatDrag();
        return true;
    }
    const InspectorPropertyId property = sceneContext.Inspector().DraggedProperty();
    const int dx = x - sceneContext.Inspector().DragStartX();
    const int dy = y - sceneContext.Inspector().DragStartY();
    if (std::abs(dx) + std::abs(dy) < 2) {
        return true;
    }
    sceneContext.Inspector().MarkFloatDragMoved();
    const float delta = static_cast<float>(dx - dy) * StepFor(property) * 0.08F;
    SetTransformValue(sceneContext.Scene(), entity, property, sceneContext.Inspector().DragStartValue() + delta);
    const std::array<kb::scene::SceneEntity, 1U> touched{ entity };
    sceneContext.MarkSceneEntitiesRenderDirty(std::span<const kb::scene::SceneEntity>{ touched.data(), touched.size() });
    return true;
}

bool InspectorPanelInteraction::HandlePointerUp(EditorSceneContext& sceneContext) noexcept {
    if (sceneContext.Inspector().IsDraggingMeshPreview()) {
        sceneContext.Inspector().EndMeshPreviewDrag();
        return true;
    }
    if (!sceneContext.Inspector().IsDraggingFloat()) {
        return false;
    }
    InspectorPanelState& inspector = sceneContext.Inspector();
    const InspectorPropertyId property = inspector.DraggedProperty();
    const bool moved = inspector.FloatDragMoved();
    const float startValue = inspector.DragStartValue();
    inspector.EndFloatDrag();
    if (moved) {
        static_cast<void>(sceneContext.CommitActiveTransformEdit());
    } else {
        sceneContext.CancelActiveTransformEdit();
    }
    if (!moved && property != InspectorPropertyId::None) {
        inspector.BeginTextEdit(property, FormatCompactFloat(startValue));
    }
    return true;
}

bool InspectorPanelInteraction::HandleChar(EditorSceneContext& sceneContext, wchar_t character) {
    if (!sceneContext.Inspector().IsTextEditing()) {
        return false;
    }
    if (character == VK_BACK || character == VK_ESCAPE || character == VK_RETURN) {
        return false;
    }
    sceneContext.Inspector().AppendText(character);
    return true;
}

bool InspectorPanelInteraction::HandleKeyDown(HWND owner, EditorSceneContext& sceneContext, WPARAM key) {
    if (!sceneContext.Inspector().IsTextEditing()) {
        return false;
    }

    InspectorPanelState& inspector = sceneContext.Inspector();
    switch (EditorTextInputShortcuts::Resolve(key)) {
    case EditorTextInputShortcut::SelectAll:
        inspector.SelectAllText();
        return true;
    case EditorTextInputShortcut::Copy:
        static_cast<void>(EditorTextInputShortcuts::CopyToClipboard(owner, inspector.EditBuffer()));
        return true;
    case EditorTextInputShortcut::Cut:
        static_cast<void>(EditorTextInputShortcuts::CopyToClipboard(owner, inspector.EditBuffer()));
        inspector.ClearText();
        return true;
    case EditorTextInputShortcut::Paste:
        if (const std::optional<std::string> text = EditorTextInputShortcuts::PasteFromClipboard(owner); text.has_value()) {
            inspector.InsertText(*text);
        }
        return true;
    case EditorTextInputShortcut::None:
        break;
    }

    switch (key) {
    case VK_BACK:
        inspector.BackspaceText();
        return true;
    case VK_ESCAPE:
        inspector.EndTextEdit();
        return true;
    case VK_RETURN: {
        if (inspector.EditedProperty() == InspectorPropertyId::InputActionName) {
            static_cast<void>(sceneContext.SetInputActionName(sceneContext.AssetBrowser().SelectedAsset(), inspector.EditBuffer()));
            inspector.EndTextEdit();
            return true;
        }
        if (inspector.EditedProperty() == InspectorPropertyId::InputComponentPriority) {
            const std::string_view text = Trim(inspector.EditBuffer());
            int priority = 0;
            if (std::from_chars(text.data(), text.data() + text.size(), priority).ec == std::errc{}) {
                static_cast<void>(sceneContext.SetInputComponentPriority(sceneContext.SelectedEntity(), priority));
            }
            inspector.EndTextEdit();
            return true;
        }
        const kb::scene::SceneEntity entity = sceneContext.SelectedEntity();
        if (sceneContext.Scene().Entities().IsAlive(entity)) {
            const InspectorPropertyId property = inspector.EditedProperty();
            if (property == InspectorPropertyId::EntityName) {
                if (sceneContext.BeginSceneEditTransaction("Rename Entity")) {
                    sceneContext.Scene().Entities().SetName(entity, inspector.EditBuffer().empty() ? "Entity" : inspector.EditBuffer());
                    static_cast<void>(sceneContext.CommitSceneEditTransaction());
                }
            } else if (IsTransformProperty(property)) {
                const kb::scene::TransformComponent transform = sceneContext.Scene().Transforms().Get(entity);
                float value = 0.0F;
                if (EvaluateMath(inspector.EditBuffer(), ReadTransformValue(transform, property), value)) {
                    if (sceneContext.BeginSelectedTransformEdit("Edit Transform")) {
                        SetTransformValue(sceneContext.Scene(), entity, property, value);
                        const std::array<kb::scene::SceneEntity, 1U> touched{ entity };
                        sceneContext.MarkSceneEntitiesRenderDirty(std::span<const kb::scene::SceneEntity>{ touched.data(), touched.size() });
                        static_cast<void>(sceneContext.CommitActiveTransformEdit());
                    }
                }
            }
        }
        inspector.EndTextEdit();
        return true;
    }
    default:
        return false;
    }
}

bool InspectorPanelInteraction::HandleKeyCapture(EditorSceneContext& sceneContext, WPARAM virtualKey) {
    return InspectorInputInteraction::HandleKeyCapture(sceneContext, virtualKey);
}

bool InspectorPanelInteraction::UpdateHover(EditorSceneContext& sceneContext, const InspectorPanelRenderer::Hit& hit) noexcept {
    return sceneContext.Inspector().SetHover(hit.kind, hit.section, hit.property);
}

bool InspectorPanelInteraction::HandleMouseWheel(EditorSceneContext& sceneContext, const InspectorPanelRenderer::Hit& hit, int wheelDelta) noexcept {
    if (hit.kind != InspectorHitKind::MeshPreview) {
        return false;
    }
    const float direction = wheelDelta > 0 ? 0.10F : -0.10F;
    return sceneContext.Inspector().ZoomMeshPreview(direction);
}

bool InspectorPanelInteraction::HandleDoubleClick(EditorSceneContext& sceneContext, const InspectorPanelRenderer::Hit& hit) noexcept {
    if (hit.kind != InspectorHitKind::MeshPreview) {
        return false;
    }
    sceneContext.Inspector().ResetMeshPreview();
    return true;
}

} // namespace kb::editor

#endif
