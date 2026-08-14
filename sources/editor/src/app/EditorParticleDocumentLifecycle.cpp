#include "app/EditorParticleDocumentLifecycle.hpp"

#if defined(_WIN32)
#include "scene/EditorSceneContext.hpp"

#include <string>

namespace kb::editor {

bool EditorParticleDocumentLifecycle::Resolve(
    HWND owner,
    EditorSceneContext& sceneContext,
    kb::particle_editor::ParticleDocumentTransition transition,
    std::wstring_view action) {
    const auto requested = sceneContext.RequestParticleEditorTransition(transition);
    if (requested.state == kb::particle_editor::ParticleDocumentCloseState::Proceed) return true;
    if (requested.state != kb::particle_editor::ParticleDocumentCloseState::DecisionRequired) return false;

    std::wstring prompt = L"Save changes to the open 21kb Particle System document before ";
    prompt += action;
    prompt += L"?\n\nYes = Save\nNo = Discard changes\nCancel = keep editing";
    kb::particle_editor::ParticleDocumentCloseDecision decision;
    switch (MessageBoxW(owner, prompt.c_str(), L"Unsaved Particle Effect",
        MB_ICONWARNING | MB_YESNOCANCEL | MB_DEFBUTTON1 | MB_APPLMODAL)) {
    case IDYES: decision = kb::particle_editor::ParticleDocumentCloseDecision::Save; break;
    case IDNO: decision = kb::particle_editor::ParticleDocumentCloseDecision::Discard; break;
    case IDCANCEL:
    default: decision = kb::particle_editor::ParticleDocumentCloseDecision::Cancel; break;
    }
    return sceneContext.ResolveParticleEditorTransition(decision).state ==
        kb::particle_editor::ParticleDocumentCloseState::Proceed;
}

} // namespace kb::editor
#endif
