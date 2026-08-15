#include "app/ParticleEditorPanelInteraction.hpp"

#include "scene/EditorSceneContext.hpp"

#include <algorithm>
#include <cstddef>
#include <string>

namespace kb::editor {

bool ParticleEditorPanelInteraction::Execute(
    EditorSceneContext& sceneContext,
    const ParticleEditorPanelHit& hit,
    kb::assets::AssetId selectedMaterial,
    std::string_view editedValue) {
    switch (hit.action) {
    case ParticleEditorPanelAction::AddEmitter:
        return selectedMaterial.IsValid() && sceneContext.AddParticleEditorEmitter(selectedMaterial);
    case ParticleEditorPanelAction::SelectEmitter:
        return sceneContext.SelectParticleEditorEmitter(hit.emitterId);
    case ParticleEditorPanelAction::ToggleEmitter:
        return sceneContext.ToggleParticleEditorEmitter(hit.emitterId);
    case ParticleEditorPanelAction::MoveEmitterUp:
        return hit.authoringOrder != 0U &&
            sceneContext.MoveParticleEditorEmitter(hit.emitterId, hit.authoringOrder - 1U);
    case ParticleEditorPanelAction::MoveEmitterDown: {
        const auto rows = sceneContext.ParticleEditorEmitterRows();
        return hit.authoringOrder + 1U < static_cast<std::uint32_t>(rows.size()) &&
            sceneContext.MoveParticleEditorEmitter(hit.emitterId, hit.authoringOrder + 1U);
    }
    case ParticleEditorPanelAction::RemoveEmitter:
        return sceneContext.RemoveParticleEditorEmitter(hit.emitterId);
    case ParticleEditorPanelAction::BeginEmitterDrag:
        return sceneContext.BeginParticleEditorEmitterDrag(hit.emitterId);
    case ParticleEditorPanelAction::SelectOutputType:
        return sceneContext.SetParticleEditorOutputType(hit.outputType);
    case ParticleEditorPanelAction::PickOutputMaterial:
        return selectedMaterial.IsValid() &&
            sceneContext.SetParticleEditorOutputReference(kb::assets::AssetKind::Material, selectedMaterial);
    case ParticleEditorPanelAction::PickOutputMesh:
        return selectedMaterial.IsValid() &&
            sceneContext.SetParticleEditorOutputReference(kb::assets::AssetKind::Mesh, selectedMaterial);
    case ParticleEditorPanelAction::PickOutputTexture:
        return selectedMaterial.IsValid() &&
            sceneContext.SetParticleEditorOutputReference(kb::assets::AssetKind::Texture, selectedMaterial);
    case ParticleEditorPanelAction::EditProperty:
        return sceneContext.EditParticleEditorProperty(hit.propertyIndex, editedValue);
    case ParticleEditorPanelAction::AddModule:
        return sceneContext.AddParticleEditorModule(hit.moduleType, hit.targetEmitterId);
    case ParticleEditorPanelAction::SelectModule:
        return sceneContext.SelectParticleEditorModule(hit.emitterId, hit.moduleId);
    case ParticleEditorPanelAction::BeginModuleDrag:
        return sceneContext.BeginParticleEditorModuleDrag(hit.moduleId);
    case ParticleEditorPanelAction::ToggleModule:
        return sceneContext.ToggleParticleEditorModule(hit.moduleId);
    case ParticleEditorPanelAction::MoveModuleUp:
        return hit.authoringOrder != 0U &&
            sceneContext.MoveParticleEditorModule(hit.moduleId, hit.authoringOrder - 1U);
    case ParticleEditorPanelAction::MoveModuleDown: {
        const auto inspector = sceneContext.ParticleEditorInspector();
        return hit.authoringOrder + 1U < inspector.modules.size() &&
            sceneContext.MoveParticleEditorModule(hit.moduleId, hit.authoringOrder + 1U);
    }
    case ParticleEditorPanelAction::RemoveModule:
        return sceneContext.RemoveParticleEditorModule(hit.moduleId);
    case ParticleEditorPanelAction::NavigateDiagnostic:
        return sceneContext.FocusParticleEditorDiagnostic(hit.diagnosticIndex);
    case ParticleEditorPanelAction::NavigateDependency:
        return sceneContext.NavigateParticleEditorDependency(hit.dependencyIndex);
    case ParticleEditorPanelAction::None:
        break;
    }
    return false;
}

void ParticleEditorPanelInteraction::UpdateDrag(
    EditorSceneContext& sceneContext,
    const ParticleEditorPanelLayout& layout,
    int y) noexcept {
    sceneContext.UpdateParticleEditorEmitterDrag(
        ParticleEditorPanelLayoutResolver::ReorderTargetAt(layout, y));
    if (sceneContext.ParticleEditorWorkspace().ModuleDragActive())
        sceneContext.UpdateParticleEditorModuleDrag(
            ParticleEditorPanelLayoutResolver::ModuleReorderTargetAt(layout, y));
}

bool ParticleEditorPanelInteraction::CommitDrag(EditorSceneContext& sceneContext) {
    if (sceneContext.ParticleEditorWorkspace().ModuleDragActive())
        return sceneContext.CommitParticleEditorModuleDrag();
    return sceneContext.CommitParticleEditorEmitterDrag();
}

bool ParticleEditorPanelInteraction::HandleCharacter(
    EditorSceneContext& sceneContext,
    wchar_t character) {
    static thread_local wchar_t pendingHighSurrogate = 0;
    if (!sceneContext.ParticleEditorWorkspace().RenameActive())
        return false;
    if (character == L'\r') {
        pendingHighSurrogate = 0;
        return sceneContext.CommitParticleEditorEmitterRename();
    }
    if (character == L'\b') {
        pendingHighSurrogate = 0;
        sceneContext.RemoveParticleEditorRenameCharacter();
        return true;
    }
    if (character < L' ' || character == 0x7f) {
        pendingHighSurrogate = 0;
        return true;
    }
    std::wstring utf16;
    if (character >= 0xd800 && character <= 0xdbff) {
        pendingHighSurrogate = character;
        return true;
    }
    if (character >= 0xdc00 && character <= 0xdfff) {
        if (pendingHighSurrogate == 0)
            return true;
        utf16.push_back(pendingHighSurrogate);
        pendingHighSurrogate = 0;
    } else {
        pendingHighSurrogate = 0;
    }
    utf16.push_back(character);
    const int required = WideCharToMultiByte(
        CP_UTF8, WC_ERR_INVALID_CHARS, utf16.data(), static_cast<int>(utf16.size()),
        nullptr, 0, nullptr, nullptr);
    if (required <= 0)
        return true;
    std::string utf8(static_cast<std::size_t>(required), '\0');
    if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, utf16.data(),
            static_cast<int>(utf16.size()), utf8.data(), required, nullptr, nullptr) == required)
        sceneContext.AppendParticleEditorRenameText(utf8);
    return true;
}

bool ParticleEditorPanelInteraction::HandleKeyDown(
    EditorSceneContext& sceneContext,
    std::uintptr_t key) {
    const auto& workspace = sceneContext.ParticleEditorWorkspace();
    if (workspace.RenameActive()) {
        if (key == VK_ESCAPE) {
            sceneContext.CancelParticleEditorEmitterRename();
            return true;
        }
        return key == VK_RETURN;
    }
    if (!workspace.Focused() || !sceneContext.HasParticleEditorAsset())
        return false;
    const kb::scene::ParticleStableId selected = workspace.SelectedEmitterId();
    if (key == VK_F2)
        return sceneContext.BeginParticleEditorEmitterRename(selected);
    if (key == VK_DELETE)
        return workspace.SelectedModuleId() != 0U
            ? sceneContext.RemoveParticleEditorModule(workspace.SelectedModuleId())
            : sceneContext.RemoveParticleEditorEmitter(selected);
    if (key == VK_SPACE)
        return workspace.SelectedModuleId() != 0U
            ? sceneContext.ToggleParticleEditorModule(workspace.SelectedModuleId())
            : sceneContext.ToggleParticleEditorEmitter(selected);
    if (key != VK_UP && key != VK_DOWN)
        return false;
    const auto rows = sceneContext.ParticleEditorEmitterRows();
    const auto found = std::find_if(rows.begin(), rows.end(), [selected](const auto& row) {
        return row.emitterId == selected;
    });
    if (found == rows.end())
        return false;
    const std::ptrdiff_t index = std::distance(rows.begin(), found);
    const std::ptrdiff_t target = key == VK_UP ? index - 1 : index + 1;
    if (target < 0 || target >= static_cast<std::ptrdiff_t>(rows.size()))
        return true;
    return sceneContext.SelectParticleEditorEmitter(rows[static_cast<std::size_t>(target)].emitterId);
}

} // namespace kb::editor
