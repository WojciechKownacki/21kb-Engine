#include "kb/render/MaterialProgramRegistry.hpp"

#include <algorithm>
#include <string_view>
#include <utility>

namespace kb::render {
namespace {

constexpr std::uint64_t kProgramKeyFnvOffset = 1469598103934665603ULL;
constexpr std::uint64_t kProgramKeyFnvPrime = 1099511628211ULL;

void HashByte(std::uint64_t& hash, std::uint8_t value) noexcept {
    hash ^= value;
    hash *= kProgramKeyFnvPrime;
}

void HashU32(std::uint64_t& hash, std::uint32_t value) noexcept {
    for (std::uint32_t shift = 0U; shift < 32U; shift += 8U) {
        HashByte(hash, static_cast<std::uint8_t>((value >> shift) & 0xFFU));
    }
}

void HashU64(std::uint64_t& hash, std::uint64_t value) noexcept {
    for (std::uint32_t shift = 0U; shift < 64U; shift += 8U) {
        HashByte(hash, static_cast<std::uint8_t>((value >> shift) & 0xFFU));
    }
}

void HashString(std::uint64_t& hash, std::string_view value) noexcept {
    HashU64(hash, static_cast<std::uint64_t>(value.size()));
    for (const char ch : value) {
        HashByte(hash, static_cast<std::uint8_t>(static_cast<unsigned char>(ch)));
    }
}

} // namespace

bool MaterialProgramKey::operator==(const MaterialProgramKey& rhs) const noexcept {
    return materialTypeId == rhs.materialTypeId &&
        materialTypeVersion == rhs.materialTypeVersion &&
        graphSourceHash == rhs.graphSourceHash &&
        variantKey == rhs.variantKey &&
        pass == rhs.pass &&
        backend == rhs.backend &&
        pipelineStateKey == rhs.pipelineStateKey &&
        requiresGeneratedVertexShader == rhs.requiresGeneratedVertexShader &&
        graphProgram == rhs.graphProgram;
}

std::uint64_t MaterialProgramKeyIdentityHash(const MaterialProgramKey& key) noexcept {
    std::uint64_t hash = kProgramKeyFnvOffset;
    HashU64(hash, key.materialTypeId);
    HashU32(hash, key.materialTypeVersion);
    HashU64(hash, key.graphSourceHash);
    HashU64(hash, key.variantKey);
    HashString(hash, key.pass);
    HashU32(hash, key.backend);
    HashU64(hash, key.pipelineStateKey);
    HashByte(hash, key.requiresGeneratedVertexShader ? 1U : 0U);
    HashByte(hash, key.graphProgram ? 1U : 0U);
    return hash;
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
