#include "app/EditorParticleDocumentLifecycle.hpp"

#if defined(_WIN32)
#include "localization/EditorLocalization.hpp"
#include "platform/win32/EditorChoiceDialog.hpp"
#include "scene/EditorSceneContext.hpp"

#include <string>

namespace kb::editor {
namespace {

[[nodiscard]] std::string_view SavePrompt(kb::particle_editor::ParticleDocumentTransition transition) {
    using Transition = kb::particle_editor::ParticleDocumentTransition;
    switch (transition) {
    case Transition::Open:
        return EditorText("particleDirty.beforeOpen", "Save changes to the open 21kb Particle System document before opening another particle effect?");
    case Transition::CloseTab:
        return EditorText("particleDirty.beforeCloseTab", "Save changes to the open 21kb Particle System document before closing the particle editor tab?");
    case Transition::CloseWindow:
        return EditorText("particleDirty.beforeCloseWindow", "Save changes to the open 21kb Particle System document before closing the particle editor window?");
    case Transition::ExitApplication:
        return EditorText("particleDirty.beforeCloseEditor", "Save changes to the open 21kb Particle System document before closing the editor?");
    case Transition::Revert:
    case Transition::CloseProject:
    default:
        return EditorText("particleDirty.question", "Save changes to the open 21kb Particle System document?");
    }
}

} // namespace

bool EditorParticleDocumentLifecycle::Resolve(
    HWND owner,
    EditorSceneContext& sceneContext,
    kb::particle_editor::ParticleDocumentTransition transition) {
    const auto requested = sceneContext.RequestParticleEditorTransition(transition);
    if (requested.state == kb::particle_editor::ParticleDocumentCloseState::Proceed) return true;
    if (requested.state != kb::particle_editor::ParticleDocumentCloseState::DecisionRequired) return false;

    kb::particle_editor::ParticleDocumentCloseDecision decision;
    switch (EditorChoiceDialog::Show(owner, EditorChoiceDialogDescriptor{
        .title = std::string{ EditorText("particleDirty.title", "Unsaved Particle Effect") },
        .message = std::string{ SavePrompt(transition) },
        .supportingText = std::string{ EditorText("particleDirty.supporting", "Choose whether to preserve the current effect changes.") },
        .primaryLabel = std::string{ EditorText("common.save", "Save") },
        .secondaryLabel = std::string{ EditorText("common.discard", "Discard") },
        .cancelLabel = std::string{ EditorText("common.cancel", "Cancel") },
        .icon = HeroIconKind::Bolt,
    })) {
    case EditorChoiceDialogResult::Primary: decision = kb::particle_editor::ParticleDocumentCloseDecision::Save; break;
    case EditorChoiceDialogResult::Secondary: decision = kb::particle_editor::ParticleDocumentCloseDecision::Discard; break;
    case EditorChoiceDialogResult::Cancel:
    default: decision = kb::particle_editor::ParticleDocumentCloseDecision::Cancel; break;
    }
    return sceneContext.ResolveParticleEditorTransition(decision).state ==
        kb::particle_editor::ParticleDocumentCloseState::Proceed;
}

} // namespace kb::editor
#endif
