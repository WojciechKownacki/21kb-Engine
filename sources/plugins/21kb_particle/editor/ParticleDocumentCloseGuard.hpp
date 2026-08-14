#pragma once

#include "ParticleEditorDocument.hpp"

#include <filesystem>
#include <optional>

namespace kb::particle_editor {

enum class ParticleDocumentTransition : std::uint8_t { Open, Revert, CloseTab, CloseWindow, CloseProject, ExitApplication };
enum class ParticleDocumentCloseDecision : std::uint8_t { Save, Discard, Cancel };
enum class ParticleDocumentCloseState : std::uint8_t { Proceed, DecisionRequired, Blocked, Cancelled };

struct ParticleDocumentCloseResult {
    ParticleDocumentCloseState state = ParticleDocumentCloseState::Proceed;
    ParticleEditorResult saveResult;
};

class ParticleDocumentCloseGuard final {
public:
    [[nodiscard]] ParticleDocumentCloseResult Request(
        const ParticleEditorDocument& document,
        ParticleDocumentTransition transition) noexcept;
    [[nodiscard]] ParticleDocumentCloseResult Resolve(
        ParticleDocumentCloseDecision decision,
        ParticleEditorDocument& document,
        const ParticleAssetGateway& gateway,
        std::optional<std::filesystem::path> savePath = std::nullopt);
    [[nodiscard]] std::optional<ParticleDocumentTransition> PendingTransition() const noexcept;

private:
    std::optional<ParticleDocumentTransition> pending_;
};

} // namespace kb::particle_editor
