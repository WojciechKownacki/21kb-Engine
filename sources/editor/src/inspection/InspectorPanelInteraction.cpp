#include "inspection/InspectorPanelInteraction.hpp"

#if defined(_WIN32)
#include "app/EditorTextInputShortcuts.hpp"
#include "assets/EditorAssetBrowserState.hpp"
#include "engine/assets/AssetId.hpp"
#include "engine/scene/AudioListenerComponent.hpp"
#include "engine/scene/AudioSourceComponent.hpp"
#include "engine/scene/SceneComponents.hpp"
#include "engine/scene/SceneEntities.hpp"
#include "engine/scene/VisibilityComponent.hpp"
#include "inspection/InspectorComponentCatalog.hpp"
#include "inspection/InspectorInputInteraction.hpp"
#include "scene/transform_edit/EditorTransformProperty.hpp"

#include <algorithm>
#include <charconv>
#include <cstdio>
#include <cstdlib>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace kb::editor {
namespace {

[[nodiscard]] bool HandleScriptClick(EditorSceneContext& sceneContext, kb::scene::SceneEntity entity, const InspectorPanelRenderer::Hit& hit) {
    sceneContext.Inspector().EndTextEdit();
    switch (hit.property) {
    case InspectorPropertyId::ScriptEnabled:
        static_cast<void>(sceneContext.ToggleEntityScriptEnabled(entity));
        return true;
    case InspectorPropertyId::ComponentRemove:
        static_cast<void>(sceneContext.RemoveScriptFromEntity(entity));
        sceneContext.Inspector().CloseComponentMenus();
        return true;
    default:
        return true;
    }
}

[[nodiscard]] bool HandleAddComponentClick(EditorSceneContext& sceneContext, kb::scene::SceneEntity entity, const InspectorPanelRenderer::Hit& hit) {
    const std::string query = sceneContext.Inspector().EditedProperty() == InspectorPropertyId::AddComponentSearch
        ? sceneContext.Inspector().EditBuffer()
        : std::string{};
    sceneContext.Inspector().CloseComponentMenus();
    if (hit.property == InspectorPropertyId::AddComponentButton) {
        sceneContext.Inspector().EndTextEdit();
        sceneContext.Inspector().ToggleAddComponentBrowser();
        return true;
    }
    if (hit.property == InspectorPropertyId::AddComponentSearch) {
        sceneContext.Inspector().BeginTextEdit(InspectorPropertyId::AddComponentSearch, query);
        return true;
    }
    if (hit.property == InspectorPropertyId::AddComponentOption) {
        const std::vector<const InspectorComponentTile*> tiles = InspectorComponentCatalog::Search(query);
        if (hit.index >= 0 && static_cast<std::size_t>(hit.index) < tiles.size()) {
            static_cast<void>(sceneContext.AddComponentToEntity(entity, tiles[static_cast<std::size_t>(hit.index)]->id));
        }
        sceneContext.Inspector().CloseAddComponentBrowser();
        return true;
    }
    return true;
}

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

[[nodiscard]] bool ToggleAudioSourceProperty(kb::scene::AudioSourceComponent& source, InspectorPropertyId property) noexcept {
    switch (property) {
    case InspectorPropertyId::AudioSourceEnabled:
        source.enabled = !source.enabled;
        return true;
    case InspectorPropertyId::AudioSourceAutoplay:
        source.autoplay = !source.autoplay;
        return true;
    case InspectorPropertyId::AudioSourceLoop:
        source.loop = !source.loop;
        return true;
    case InspectorPropertyId::AudioSourceMute:
        source.mute = !source.mute;
        return true;
    case InspectorPropertyId::AudioSourceSpatial:
        source.spatial = !source.spatial;
        return true;
    default:
        return false;
    }
}

[[nodiscard]] bool ToggleAudioListenerProperty(kb::scene::AudioListenerComponent& listener, InspectorPropertyId property) noexcept {
    switch (property) {
    case InspectorPropertyId::AudioListenerEnabled:
        listener.enabled = !listener.enabled;
        return true;
    case InspectorPropertyId::AudioListenerPrimary:
        listener.primary = !listener.primary;
        return true;
    default:
        return false;
    }
}

[[nodiscard]] bool ToggleAudioProperty(EditorSceneContext& sceneContext, kb::scene::SceneEntity entity, InspectorPropertyId property) {
    if (!sceneContext.BeginSceneEditTransaction("Edit Audio Component")) {
        return true;
    }

    bool changed = false;
    if (kb::scene::AudioSourceComponent* source = sceneContext.Scene().Components().AudioSources().TryGet(entity); source != nullptr) {
        changed = ToggleAudioSourceProperty(*source, property);
        if (changed) {
            sceneContext.Scene().Components().AudioSources().MarkModified(entity);
        }
    }

    if (!changed) {
        if (kb::scene::AudioListenerComponent* listener = sceneContext.Scene().Components().AudioListeners().TryGet(entity); listener != nullptr) {
            changed = ToggleAudioListenerProperty(*listener, property);
            if (changed) {
                sceneContext.Scene().Components().AudioListeners().MarkModified(entity);
            }
        }
    }

    if (changed) {
        static_cast<void>(sceneContext.CommitSceneEditTransaction());
    } else {
        sceneContext.CancelSceneEditTransaction();
    }
    return true;
}

} // namespace

bool InspectorPanelInteraction::HandlePointerDown(EditorSceneContext& sceneContext, const InspectorPanelRenderer::Hit& hit, int x, int y) noexcept {
    if (hit.kind == InspectorHitKind::None) {
        sceneContext.Inspector().EndTextEdit();
        sceneContext.Inspector().CloseAddComponentBrowser();
        sceneContext.Inspector().CloseComponentMenus();
        return false;
    }

    kb::scene::SceneEntity entity = sceneContext.SelectedEntity();
    if (hit.kind == InspectorHitKind::SectionHeader) {
        sceneContext.Inspector().EndTextEdit();
        sceneContext.Inspector().CloseAddComponentBrowser();
        sceneContext.Inspector().CloseComponentMenus();
        sceneContext.Inspector().ToggleCollapsed(hit.section);
        return true;
    }
    if (hit.kind == InspectorHitKind::ComponentMenuButton) {
        sceneContext.Inspector().EndTextEdit();
        if (hit.section == InspectorSectionId::Script && hit.property == InspectorPropertyId::ComponentRemove && sceneContext.Scene().Entities().IsAlive(entity)) {
            static_cast<void>(sceneContext.RemoveScriptFromEntity(entity));
        }
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

    if (hit.section == InspectorSectionId::Script) {
        return HandleScriptClick(sceneContext, entity, hit);
    }
    if (hit.section == InspectorSectionId::AddComponent) {
        return HandleAddComponentClick(sceneContext, entity, hit);
    }

    if (hit.kind == InspectorHitKind::TextField && hit.property == InspectorPropertyId::EntityName) {
        sceneContext.Inspector().BeginTextEdit(InspectorPropertyId::EntityName, sceneContext.Scene().Entities().Name(entity));
        return true;
    }
    if (hit.kind == InspectorHitKind::BoolField) {
        sceneContext.Inspector().EndTextEdit();
        if (hit.property == InspectorPropertyId::EntityVisible) {
            static_cast<void>(sceneContext.ToggleEntityVisibility(entity));
        } else if (hit.section == InspectorSectionId::AudioSource || hit.section == InspectorSectionId::AudioListener) {
            return ToggleAudioProperty(sceneContext, entity, hit.property);
        }
        return true;
    }
    if (hit.kind == InspectorHitKind::FloatField) {
        if (!sceneContext.BeginSelectedTransformEdit("Edit Transform")) {
            return true;
        }
        sceneContext.Inspector().BeginFloatDrag(hit.property, sceneContext.ActiveTransformEditPropertyStart(hit.property), x, y);
        return true;
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
    static_cast<void>(sceneContext.ApplyActiveTransformEditProperty(property, sceneContext.Inspector().DragStartValue() + delta));
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
        if (inspector.EditedProperty() == InspectorPropertyId::AddComponentSearch) {
            inspector.CloseAddComponentBrowser();
            return true;
        }
        inspector.EndTextEdit();
        return true;
    case VK_RETURN: {
        if (inspector.EditedProperty() == InspectorPropertyId::AddComponentSearch) {
            const std::vector<const InspectorComponentTile*> tiles = InspectorComponentCatalog::Search(inspector.EditBuffer());
            const kb::scene::SceneEntity entity = sceneContext.SelectedEntity();
            if (!tiles.empty() && sceneContext.Scene().Entities().IsAlive(entity)) {
                static_cast<void>(sceneContext.AddComponentToEntity(entity, tiles.front()->id));
            }
            inspector.CloseAddComponentBrowser();
            return true;
        }
        if (inspector.EditedProperty() == InspectorPropertyId::InputActionName) {
            static_cast<void>(sceneContext.SetInputActionName(sceneContext.AssetBrowser().InspectorAsset(), inspector.EditBuffer()));
            inspector.EndTextEdit();
            return true;
        }
        if (inspector.EditedProperty() == InspectorPropertyId::InputMappingScale) {
            float scale = 0.0F;
            const int index = inspector.EditIndex();
            if (index >= 0 && ParseFloat(inspector.EditBuffer(), scale)) {
                static_cast<void>(sceneContext.SetInputMappingScale(sceneContext.AssetBrowser().InspectorAsset(), static_cast<std::size_t>(index), scale));
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
            } else if (EditorTransformProperty::IsTransform(property)) {
                if (sceneContext.BeginSelectedTransformEdit("Edit Transform")) {
                    float value = 0.0F;
                    if (EvaluateMath(inspector.EditBuffer(), sceneContext.ActiveTransformEditPropertyStart(property), value)) {
                        static_cast<void>(sceneContext.ApplyActiveTransformEditProperty(property, value));
                        static_cast<void>(sceneContext.CommitActiveTransformEdit());
                    } else {
                        sceneContext.CancelActiveTransformEdit();
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
    const int dropdownHover = hit.kind == InspectorHitKind::ValueTypeOption ? hit.index : -1;
    const int previousDropdownHover = sceneContext.Inspector().ValueTypeDropdownHover();
    sceneContext.Inspector().SetValueTypeDropdownHover(dropdownHover);
    const bool hoverChanged = sceneContext.Inspector().SetHover(hit.kind, hit.section, hit.property);
    return hoverChanged || previousDropdownHover != dropdownHover;
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
