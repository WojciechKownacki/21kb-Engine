#include "kb/render/MaterialProgramRegistry.hpp"

#include <algorithm>
#include <utility>

namespace kb::render {

bool MaterialProgramKey::operator==(const MaterialProgramKey& rhs) const noexcept {
    return materialTypeId == rhs.materialTypeId &&
        materialTypeVersion == rhs.materialTypeVersion &&
        graphSourceHash == rhs.graphSourceHash &&
        pass == rhs.pass &&
        backend == rhs.backend &&
        pipelineStateKey == rhs.pipelineStateKey &&
        graphProgram == rhs.graphProgram;
}

void MaterialProgramRegistry::Configure(MaterialProgramLoader loader, MaterialProgramDestroyer destroyer, std::uint32_t graceFrames) {
    loader_ = std::move(loader);
    destroyer_ = std::move(destroyer);
    graceFrames_ = graceFrames;
}

bool MaterialProgramRegistry::IsConfigured() const noexcept {
    return static_cast<bool>(loader_) && static_cast<bool>(destroyer_);
}

MaterialProgramRegistry::Entry* MaterialProgramRegistry::FindEntry(const MaterialProgramKey& key) noexcept {
    for (Entry& entry : entries_) {
        if (entry.key == key) {
            return &entry;
        }
    }
    return nullptr;
}

const MaterialProgramRegistry::Entry* MaterialProgramRegistry::FindEntry(const MaterialProgramKey& key) const noexcept {
    for (const Entry& entry : entries_) {
        if (entry.key == key) {
            return &entry;
        }
    }
    return nullptr;
}

void MaterialProgramRegistry::DestroyHandle(bgfx::ProgramHandle handle) {
    if (bgfx::isValid(handle) && destroyer_) {
        destroyer_(handle);
        ++stats_.destroyedProgramCount;
    }
}

void MaterialProgramRegistry::OrphanHandle(bgfx::ProgramHandle handle) {
    if (!bgfx::isValid(handle)) {
        return;
    }
    pendingHandles_.push_back(PendingHandle{
        .handle = handle,
        .destroyFrame = currentFrame_ + graceFrames_,
    });
}

bgfx::ProgramHandle MaterialProgramRegistry::Acquire(const MaterialProgramKey& key) {
    Entry* entry = FindEntry(key);
    if (entry != nullptr) {
        entry->pendingDestroy = false;
        if (bgfx::isValid(entry->handle)) {
            ++entry->refCount;
            ++stats_.hits;
            return entry->handle;
        }
        const bgfx::ProgramHandle handle = loader_ ? loader_(key) : bgfx::ProgramHandle{ BGFX_INVALID_HANDLE };
        if (bgfx::isValid(handle)) {
            entry->handle = handle;
            entry->lastGood = handle;
            ++entry->refCount;
            ++stats_.loads;
            return handle;
        }
        ++stats_.failures;
        return BGFX_INVALID_HANDLE;
    }

    ++stats_.misses;
    const bgfx::ProgramHandle handle = loader_ ? loader_(key) : bgfx::ProgramHandle{ BGFX_INVALID_HANDLE };
    if (!bgfx::isValid(handle)) {
        ++stats_.failures;
        return BGFX_INVALID_HANDLE;
    }
    entries_.push_back(Entry{
        .key = key,
        .handle = handle,
        .lastGood = handle,
        .refCount = 1U,
    });
    ++stats_.loads;
    return handle;
}

void MaterialProgramRegistry::Release(const MaterialProgramKey& key) {
    Entry* entry = FindEntry(key);
    if (entry == nullptr) {
        return;
    }
    if (entry->refCount > 0U) {
        --entry->refCount;
    }
    if (entry->refCount == 0U) {
        entry->pendingDestroy = true;
        entry->destroyFrame = currentFrame_ + graceFrames_;
    }
}

bgfx::ProgramHandle MaterialProgramRegistry::Reload(const MaterialProgramKey& key) {
    Entry* entry = FindEntry(key);
    if (entry == nullptr) {
        return Acquire(key);
    }

    const bgfx::ProgramHandle handle = loader_ ? loader_(key) : bgfx::ProgramHandle{ BGFX_INVALID_HANDLE };
    if (bgfx::isValid(handle)) {
        if (bgfx::isValid(entry->handle) && entry->handle.idx != handle.idx) {
            OrphanHandle(entry->handle);
        }
        entry->handle = handle;
        entry->lastGood = handle;
        entry->pendingDestroy = false;
        ++stats_.reloads;
        return handle;
    }

    ++stats_.failures;
    if (bgfx::isValid(entry->lastGood)) {
        entry->handle = entry->lastGood;
        ++stats_.lastGoodUses;
        return entry->lastGood;
    }
    return BGFX_INVALID_HANDLE;
}

bgfx::ProgramHandle MaterialProgramRegistry::Find(const MaterialProgramKey& key) const noexcept {
    const Entry* entry = FindEntry(key);
    return entry != nullptr ? entry->handle : bgfx::ProgramHandle{ BGFX_INVALID_HANDLE };
}

std::uint32_t MaterialProgramRegistry::RefCount(const MaterialProgramKey& key) const noexcept {
    const Entry* entry = FindEntry(key);
    return entry != nullptr ? entry->refCount : 0U;
}

void MaterialProgramRegistry::BeginFrame(std::uint64_t frameIndex) {
    currentFrame_ = frameIndex;

    const auto orphanEnd = std::remove_if(pendingHandles_.begin(), pendingHandles_.end(), [this, frameIndex](const PendingHandle& pending) {
        if (frameIndex >= pending.destroyFrame) {
            DestroyHandle(pending.handle);
            return true;
        }
        return false;
    });
    pendingHandles_.erase(orphanEnd, pendingHandles_.end());

    const auto entryEnd = std::remove_if(entries_.begin(), entries_.end(), [this, frameIndex](Entry& entry) {
        if (entry.pendingDestroy && frameIndex >= entry.destroyFrame) {
            DestroyHandle(entry.handle);
            if (bgfx::isValid(entry.lastGood) && entry.lastGood.idx != entry.handle.idx) {
                DestroyHandle(entry.lastGood);
            }
            return true;
        }
        return false;
    });
    entries_.erase(entryEnd, entries_.end());
}

void MaterialProgramRegistry::Shutdown() {
    for (Entry& entry : entries_) {
        DestroyHandle(entry.handle);
        if (bgfx::isValid(entry.lastGood) && entry.lastGood.idx != entry.handle.idx) {
            DestroyHandle(entry.lastGood);
        }
    }
    entries_.clear();
    for (const PendingHandle& pending : pendingHandles_) {
        DestroyHandle(pending.handle);
    }
    pendingHandles_.clear();
}

MaterialProgramRegistryStats MaterialProgramRegistry::Stats() const noexcept {
    MaterialProgramRegistryStats stats = stats_;
    stats.liveProgramCount = 0U;
    stats.pendingDestroyCount = static_cast<std::uint32_t>(pendingHandles_.size());
    for (const Entry& entry : entries_) {
        if (entry.pendingDestroy) {
            ++stats.pendingDestroyCount;
        } else if (bgfx::isValid(entry.handle)) {
            ++stats.liveProgramCount;
        }
    }
    return stats;
}

} // namespace kb::render
