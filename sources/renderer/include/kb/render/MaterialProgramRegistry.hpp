#pragma once

#include <bgfx/bgfx.h>

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace kb::render {

struct MaterialProgramKey {
    std::uint64_t materialTypeId = 0U;
    std::uint32_t materialTypeVersion = 0U;
    std::uint64_t graphSourceHash = 0U;
    std::string pass;
    std::uint32_t backend = 0U;
    std::uint64_t pipelineStateKey = 0U;
    bool graphProgram = false;

    [[nodiscard]] bool operator==(const MaterialProgramKey& rhs) const noexcept;
};

struct MaterialProgramRegistryStats {
    std::uint32_t hits = 0U;
    std::uint32_t misses = 0U;
    std::uint32_t loads = 0U;
    std::uint32_t failures = 0U;
    std::uint32_t reloads = 0U;
    std::uint32_t lastGoodUses = 0U;
    std::uint32_t liveProgramCount = 0U;
    std::uint32_t pendingDestroyCount = 0U;
    std::uint32_t destroyedProgramCount = 0U;
};

using MaterialProgramLoader = std::function<bgfx::ProgramHandle(const MaterialProgramKey&)>;
using MaterialProgramDestroyer = std::function<void(bgfx::ProgramHandle)>;

class MaterialProgramRegistry {
public:
    void Configure(MaterialProgramLoader loader, MaterialProgramDestroyer destroyer, std::uint32_t graceFrames = 2U);
    [[nodiscard]] bool IsConfigured() const noexcept;

    [[nodiscard]] bgfx::ProgramHandle Acquire(const MaterialProgramKey& key);
    void Release(const MaterialProgramKey& key);
    [[nodiscard]] bgfx::ProgramHandle Reload(const MaterialProgramKey& key);
    [[nodiscard]] bgfx::ProgramHandle Find(const MaterialProgramKey& key) const noexcept;
    [[nodiscard]] std::uint32_t RefCount(const MaterialProgramKey& key) const noexcept;

    void BeginFrame(std::uint64_t frameIndex);
    void Shutdown();

    [[nodiscard]] MaterialProgramRegistryStats Stats() const noexcept;

private:
    struct Entry {
        MaterialProgramKey key;
        bgfx::ProgramHandle handle = BGFX_INVALID_HANDLE;
        bgfx::ProgramHandle lastGood = BGFX_INVALID_HANDLE;
        std::uint32_t refCount = 0U;
        bool pendingDestroy = false;
        std::uint64_t destroyFrame = 0U;
    };

    struct PendingHandle {
        bgfx::ProgramHandle handle = BGFX_INVALID_HANDLE;
        std::uint64_t destroyFrame = 0U;
    };

    [[nodiscard]] Entry* FindEntry(const MaterialProgramKey& key) noexcept;
    [[nodiscard]] const Entry* FindEntry(const MaterialProgramKey& key) const noexcept;
    void DestroyHandle(bgfx::ProgramHandle handle);
    void OrphanHandle(bgfx::ProgramHandle handle);

    MaterialProgramLoader loader_;
    MaterialProgramDestroyer destroyer_;
    std::uint32_t graceFrames_ = 2U;
    std::uint64_t currentFrame_ = 0U;
    std::vector<Entry> entries_;
    std::vector<PendingHandle> pendingHandles_;
    MaterialProgramRegistryStats stats_{};
};

} // namespace kb::render
