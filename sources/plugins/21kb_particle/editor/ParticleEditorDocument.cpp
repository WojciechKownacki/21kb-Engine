#include "ParticleEditorDocument.hpp"

#include "ParticleAssetGateway.hpp"
#include "engine/scene/ParticleEffectAssetIO.hpp"

#include <stdexcept>
#include <utility>

namespace kb::particle_editor {
namespace {

[[nodiscard]] ParticleEditorResult ValidateAndCanonicalize(
    const kb::scene::ParticleEffectAsset& asset,
    std::string& canonical) {
    auto validation = kb::scene::ParticleEffectAssetValidator::ValidateStructure(asset);
    if (!validation.Succeeded()) {
        return { .status = ParticleEditorStatus::InvalidAsset,
                 .message = "particle effect failed structural validation",
                 .diagnostics = std::move(validation.diagnostics) };
    }
    std::vector<kb::scene::ParticleEffectDiagnostic> diagnostics;
    auto serialized = kb::scene::ParticleEffectAssetIO::Serialize(asset, diagnostics);
    if (!serialized.has_value()) {
        return { .status = ParticleEditorStatus::InvalidAsset,
                 .message = "particle effect could not be serialized canonically",
                 .diagnostics = std::move(diagnostics) };
    }
    canonical = std::move(*serialized);
    return {};
}

} // namespace

ParticleEditorResult ParticleEditorCommandStack::Reset(
    kb::scene::ParticleEffectAsset asset,
    bool saved) {
    std::string canonical;
    ParticleEditorResult result = ValidateAndCanonicalize(asset, canonical);
    if (!result.Succeeded()) return result;
    entries_.clear();
    entries_.push_back({ .asset = std::move(asset), .canonical = std::move(canonical) });
    cursor_ = 0U;
    saved_ = saved ? std::optional<Entry>{entries_.front()} : std::nullopt;
    return result;
}

ParticleEditorResult ParticleEditorCommandStack::Apply(kb::scene::ParticleEffectAsset asset) {
    std::string canonical;
    ParticleEditorResult result = ValidateAndCanonicalize(asset, canonical);
    if (!result.Succeeded()) return result;
    if (!entries_.empty() && entries_[cursor_].canonical == canonical) {
        result.status = ParticleEditorStatus::NoChange;
        return result;
    }
    if (cursor_ + 1U < entries_.size()) {
        entries_.erase(entries_.begin() + static_cast<std::ptrdiff_t>(cursor_ + 1U), entries_.end());
    }
    if (entries_.size() >= kMaximumEntries) {
        result.status = ParticleEditorStatus::HistoryLimit;
        result.message = "particle document history reached its bounded capacity";
        return result;
    }
    entries_.push_back({ .asset = std::move(asset), .canonical = std::move(canonical) });
    cursor_ = entries_.size() - 1U;
    return result;
}

bool ParticleEditorCommandStack::CanUndo() const noexcept { return !entries_.empty() && cursor_ != 0U; }
bool ParticleEditorCommandStack::CanRedo() const noexcept { return cursor_ + 1U < entries_.size(); }
bool ParticleEditorCommandStack::Undo() noexcept {
    if (!CanUndo()) return false;
    --cursor_;
    return true;
}
bool ParticleEditorCommandStack::Redo() noexcept {
    if (!CanRedo()) return false;
    ++cursor_;
    return true;
}
bool ParticleEditorCommandStack::Dirty() const noexcept {
    return !entries_.empty() && (!saved_.has_value() || entries_[cursor_].canonical != saved_->canonical);
}
const kb::scene::ParticleEffectAsset& ParticleEditorCommandStack::Current() const noexcept {
    return entries_[cursor_].asset;
}
bool ParticleEditorCommandStack::RevertToSavePoint() {
    if (!saved_.has_value()) return false;
    entries_.clear();
    entries_.push_back(*saved_);
    cursor_ = 0U;
    return true;
}
void ParticleEditorCommandStack::MarkSaved() { saved_ = entries_[cursor_]; }

ParticleEditorResult ParticleEditorDocument::Create(kb::scene::ParticleEffectAsset asset) {
    ParticleEditorCommandStack candidate;
    ParticleEditorResult result = candidate.Reset(std::move(asset), false);
    if (!result.Succeeded()) return result;
    history_ = std::move(candidate);
    sessionPath_.reset();
    hasDocument_ = true;
    return result;
}

ParticleEditorResult ParticleEditorDocument::Open(
    const ParticleAssetGateway& gateway,
    const std::filesystem::path& path) {
    auto loaded = gateway.Load(path);
    if (!loaded.result.Succeeded()) return std::move(loaded.result);
    ParticleEditorCommandStack candidate;
    ParticleEditorResult result = candidate.Reset(std::move(*loaded.asset), true);
    if (!result.Succeeded()) return result;
    history_ = std::move(candidate);
    sessionPath_ = path;
    hasDocument_ = true;
    return result;
}

ParticleEditorResult ParticleEditorDocument::Apply(kb::scene::ParticleEffectAsset asset) {
    if (!hasDocument_) return { .status = ParticleEditorStatus::InvalidAsset, .message = "no particle document is open" };
    return history_.Apply(std::move(asset));
}
bool ParticleEditorDocument::Undo() noexcept { return hasDocument_ && history_.Undo(); }
bool ParticleEditorDocument::Redo() noexcept { return hasDocument_ && history_.Redo(); }
bool ParticleEditorDocument::Revert() { return hasDocument_ && history_.RevertToSavePoint(); }

ParticleEditorResult ParticleEditorDocument::Save(
    const ParticleAssetGateway& gateway,
    std::optional<std::filesystem::path> path) {
    if (!hasDocument_) return { .status = ParticleEditorStatus::InvalidAsset, .message = "no particle document is open" };
    const std::optional<std::filesystem::path> target = path.has_value() ? std::move(path) : sessionPath_;
    if (!target.has_value() || target->empty()) {
        return { .status = ParticleEditorStatus::PathRequired, .message = "a path is required for the first particle document save" };
    }
    ParticleEditorResult result = gateway.Save(*target, history_.Current());
    if (!result.Succeeded()) return result;
    history_.MarkSaved();
    sessionPath_ = *target;
    return result;
}

bool ParticleEditorDocument::HasDocument() const noexcept { return hasDocument_; }
bool ParticleEditorDocument::Dirty() const noexcept { return hasDocument_ && history_.Dirty(); }
bool ParticleEditorDocument::CanUndo() const noexcept { return hasDocument_ && history_.CanUndo(); }
bool ParticleEditorDocument::CanRedo() const noexcept { return hasDocument_ && history_.CanRedo(); }
const kb::scene::ParticleEffectAsset& ParticleEditorDocument::Asset() const {
    if (!hasDocument_) throw std::logic_error{"no particle document is open"};
    return history_.Current();
}
const std::optional<std::filesystem::path>& ParticleEditorDocument::SessionPath() const noexcept { return sessionPath_; }

} // namespace kb::particle_editor
