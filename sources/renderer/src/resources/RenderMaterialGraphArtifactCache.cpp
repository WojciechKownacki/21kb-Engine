#include "kb/render/resources/RenderMaterialGraphDocument.hpp"

#include <algorithm>
#include <utility>

namespace kb::render {

bool RenderMaterialGraphIrBuildResult::Succeeded() const noexcept {
    return std::none_of(diagnostics.begin(), diagnostics.end(), [](const RenderMaterialGraphDiagnostic& diagnostic) {
        return diagnostic.severity == RenderMaterialGraphDiagnosticSeverity::Error;
    });
}

const RenderMaterialGraphFunctionLibraryEntry* RenderMaterialGraphFunctionLibrary::Find(std::uint64_t assetId) const noexcept {
    for (const RenderMaterialGraphFunctionLibraryEntry& entry : entries) {
        if (entry.assetId == assetId) {
            return &entry;
        }
    }
    return nullptr;
}

bool RenderMaterialGraphFunctionInlineResult::Succeeded() const noexcept {
    return std::none_of(diagnostics.begin(), diagnostics.end(), [](const RenderMaterialGraphDiagnostic& diagnostic) {
        return diagnostic.severity == RenderMaterialGraphDiagnosticSeverity::Error;
    });
}

bool RenderMaterialGraphCompileResult::Succeeded() const noexcept {
    return std::none_of(diagnostics.begin(), diagnostics.end(), [](const RenderMaterialGraphDiagnostic& diagnostic) {
        return diagnostic.severity == RenderMaterialGraphDiagnosticSeverity::Error;
    });
}

bool RenderMaterialGraphCompileArtifactCacheKey::operator==(const RenderMaterialGraphCompileArtifactCacheKey& rhs) const noexcept {
    return graphContentHash == rhs.graphContentHash &&
        dependencyHash == rhs.dependencyHash &&
        shaderIncludeHash == rhs.shaderIncludeHash &&
        combinedHash == rhs.combinedHash;
}

bool RenderMaterialGraphMaterialTypeBuildResult::Succeeded() const noexcept {
    return document.has_value() && std::none_of(diagnostics.begin(), diagnostics.end(), [](const RenderMaterialGraphDiagnostic& diagnostic) {
        return diagnostic.severity == RenderMaterialGraphDiagnosticSeverity::Error;
    });
}

const RenderMaterialGraphCompileArtifact* RenderMaterialGraphCompileArtifactCache::Find(const RenderMaterialGraphCompileArtifactCacheKey& key) const noexcept {
    for (const RenderMaterialGraphCompileArtifact& artifact : artifacts_) {
        if (artifact.key == key) {
            return &artifact;
        }
    }
    return nullptr;
}

void RenderMaterialGraphCompileArtifactCache::Store(RenderMaterialGraphCompileArtifact artifact) {
    for (RenderMaterialGraphCompileArtifact& existing : artifacts_) {
        if (existing.key == artifact.key) {
            existing = std::move(artifact);
            return;
        }
    }
    artifacts_.push_back(std::move(artifact));
    EnforceCapacity();
}

void RenderMaterialGraphCompileArtifactCache::SetCapacity(std::size_t maxEntries) noexcept {
    maxEntries_ = maxEntries;
    EnforceCapacity();
}

std::size_t RenderMaterialGraphCompileArtifactCache::Capacity() const noexcept {
    return maxEntries_;
}

std::size_t RenderMaterialGraphCompileArtifactCache::EvictionCount() const noexcept {
    return evictionCount_;
}

void RenderMaterialGraphCompileArtifactCache::EnforceCapacity() noexcept {
    if (maxEntries_ == 0U) {
        return;
    }
    while (artifacts_.size() > maxEntries_) {
        artifacts_.erase(artifacts_.begin());
        ++evictionCount_;
    }
}

bool RenderMaterialGraphCompileArtifactCache::Invalidate(const RenderMaterialGraphCompileArtifactCacheKey& key) {
    const auto oldEnd = std::remove_if(artifacts_.begin(), artifacts_.end(), [&key](const RenderMaterialGraphCompileArtifact& artifact) {
        return artifact.key == key;
    });
    if (oldEnd == artifacts_.end()) {
        return false;
    }
    artifacts_.erase(oldEnd, artifacts_.end());
    return true;
}

std::size_t RenderMaterialGraphCompileArtifactCache::InvalidateGraphContentHash(std::uint64_t graphContentHash) {
    const std::size_t before = artifacts_.size();
    const auto oldEnd = std::remove_if(artifacts_.begin(), artifacts_.end(), [graphContentHash](const RenderMaterialGraphCompileArtifact& artifact) {
        return artifact.key.graphContentHash == graphContentHash;
    });
    artifacts_.erase(oldEnd, artifacts_.end());
    return before - artifacts_.size();
}

void RenderMaterialGraphCompileArtifactCache::Clear() noexcept {
    artifacts_.clear();
    evictionCount_ = 0U;
}

std::size_t RenderMaterialGraphCompileArtifactCache::Size() const noexcept {
    return artifacts_.size();
}

} // namespace kb::render
