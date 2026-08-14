#include "ParticleDocumentCloseGuard.hpp"

#include "ParticleAssetGateway.hpp"

namespace kb::particle_editor {

ParticleDocumentCloseResult ParticleDocumentCloseGuard::Request(
    const ParticleEditorDocument& document,
    ParticleDocumentTransition transition) noexcept {
    if (!document.Dirty()) {
        pending_.reset();
        return {};
    }
    pending_ = transition;
    return { .state = ParticleDocumentCloseState::DecisionRequired };
}

ParticleDocumentCloseResult ParticleDocumentCloseGuard::Resolve(
    ParticleDocumentCloseDecision decision,
    ParticleEditorDocument& document,
    const ParticleAssetGateway& gateway,
    std::optional<std::filesystem::path> savePath) {
    if (!pending_.has_value()) return {};
    if (decision == ParticleDocumentCloseDecision::Cancel) {
        pending_.reset();
        return { .state = ParticleDocumentCloseState::Cancelled };
    }
    if (decision == ParticleDocumentCloseDecision::Save) {
        ParticleEditorResult saved = document.Save(gateway, std::move(savePath));
        if (!saved.Succeeded()) {
            return { .state = ParticleDocumentCloseState::Blocked, .saveResult = std::move(saved) };
        }
    }
    pending_.reset();
    return {};
}

std::optional<ParticleDocumentTransition> ParticleDocumentCloseGuard::PendingTransition() const noexcept {
    return pending_;
}

} // namespace kb::particle_editor
