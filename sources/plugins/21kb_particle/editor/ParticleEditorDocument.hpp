#pragma once

#include "engine/scene/ParticleEffectAsset.hpp"
#include "engine/scene/ParticleEffectAssetValidation.hpp"

#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace kb::particle_editor {

enum class ParticleEditorStatus : std::uint8_t {
    Success,
    NoChange,
    InvalidAsset,
    HistoryLimit,
    PathRequired,
    IoFailure,
    ProviderUnavailable,
    PublicationFailed,
    RuntimeFailure,
};

struct ParticleEditorResult {
    ParticleEditorStatus status = ParticleEditorStatus::Success;
    std::string message;
    std::vector<kb::scene::ParticleEffectDiagnostic> diagnostics;

    [[nodiscard]] bool Succeeded() const noexcept {
        return status == ParticleEditorStatus::Success || status == ParticleEditorStatus::NoChange;
    }
};

class ParticleAssetGateway;

class ParticleEditorCommandStack final {
public:
    static constexpr std::size_t kMaximumEntries = 256U;

    [[nodiscard]] ParticleEditorResult Reset(kb::scene::ParticleEffectAsset asset, bool saved);
    [[nodiscard]] ParticleEditorResult Apply(kb::scene::ParticleEffectAsset asset);
    [[nodiscard]] bool CanUndo() const noexcept;
    [[nodiscard]] bool CanRedo() const noexcept;
    [[nodiscard]] bool Undo() noexcept;
    [[nodiscard]] bool Redo() noexcept;
    [[nodiscard]] bool Dirty() const noexcept;
    [[nodiscard]] const kb::scene::ParticleEffectAsset& Current() const noexcept;
    [[nodiscard]] bool RevertToSavePoint();
    void MarkSaved();

private:
    struct Entry {
        kb::scene::ParticleEffectAsset asset;
        std::string canonical;
    };

    std::vector<Entry> entries_;
    std::size_t cursor_ = 0U;
    std::optional<Entry> saved_;
};

class ParticleEditorDocument final {
public:
    [[nodiscard]] ParticleEditorResult Create(kb::scene::ParticleEffectAsset asset);
    [[nodiscard]] ParticleEditorResult Open(const ParticleAssetGateway& gateway,
                                            const std::filesystem::path& path);
    [[nodiscard]] ParticleEditorResult Apply(kb::scene::ParticleEffectAsset asset);
    [[nodiscard]] bool Undo() noexcept;
    [[nodiscard]] bool Redo() noexcept;
    [[nodiscard]] bool Revert();
    [[nodiscard]] ParticleEditorResult Save(const ParticleAssetGateway& gateway,
                                            std::optional<std::filesystem::path> path = std::nullopt);

    [[nodiscard]] bool HasDocument() const noexcept;
    [[nodiscard]] bool Dirty() const noexcept;
    [[nodiscard]] bool CanUndo() const noexcept;
    [[nodiscard]] bool CanRedo() const noexcept;
    [[nodiscard]] const kb::scene::ParticleEffectAsset& Asset() const;
    [[nodiscard]] const std::optional<std::filesystem::path>& SessionPath() const noexcept;

private:
    ParticleEditorCommandStack history_;
    std::optional<std::filesystem::path> sessionPath_;
    bool hasDocument_ = false;
};

} // namespace kb::particle_editor
