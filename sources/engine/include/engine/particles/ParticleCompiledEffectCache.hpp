#pragma once

#include "engine/particles/ParticleCompiledEffect.hpp"

#include <cstdint>
#include <filesystem>
#include <span>
#include <string_view>

namespace kb::particles {

inline constexpr std::uint64_t kParticleCompiledEffectCacheFormatVersion = 1U;
inline constexpr std::uint64_t kParticleCompiledEffectCacheMaxBytes = 512U * 1024U;

struct ParticleCompiledEffectCacheKey {
    std::uint64_t sourceHash = 0U;
    std::uint64_t dependencyHash = 0U;
    std::uint64_t compilerVersion = kParticleCompiledEffectVersion;
    ParticleCompilePlatform platform = ParticleCompilePlatform::PlatformIndependent;
    std::uint64_t capabilityKey = 0U;

    [[nodiscard]] bool operator==(const ParticleCompiledEffectCacheKey&) const noexcept = default;
    [[nodiscard]] std::uint64_t StableHash() const noexcept;
};

enum class ParticleCompiledEffectCacheStatus : std::uint8_t {
    Success,
    Missing,
    SourceTooLarge,
    InvalidCache,
    FileAccessFailed,
    AtomicWriteFailed,
};

struct ParticleCompiledEffectCacheResult {
    ParticleCompiledEffectCacheStatus status = ParticleCompiledEffectCacheStatus::InvalidCache;
    ParticleCompiledEffectHandle effect;

    [[nodiscard]] bool Succeeded() const noexcept {
        return status == ParticleCompiledEffectCacheStatus::Success && effect != nullptr;
    }
};

class ParticleCompiledEffectCache final {
  public:
    ParticleCompiledEffectCache() = delete;
    [[nodiscard]] static std::uint64_t HashBytes(std::span<const std::uint8_t> bytes) noexcept;
    [[nodiscard]] static std::uint64_t HashText(std::string_view text) noexcept;
    [[nodiscard]] static std::filesystem::path PathFor(const std::filesystem::path& cacheRoot,
                                                       const ParticleCompiledEffectCacheKey& key);
    [[nodiscard]] static ParticleCompiledEffectCacheResult Load(const std::filesystem::path& path,
                                                                const ParticleCompiledEffectCacheKey& expectedKey);
    [[nodiscard]] static ParticleCompiledEffectCacheStatus Save(const std::filesystem::path& path,
                                                                const ParticleCompiledEffectCacheKey& key,
                                                                const ParticleCompiledEffect& effect);
};

} // namespace kb::particles
