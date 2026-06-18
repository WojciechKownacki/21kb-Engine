#pragma once

#include "engine/ecs/WorldConfig.hpp"

namespace kb::ecs {

class WorldConfigPresets {
public:
    [[nodiscard]] static constexpr WorldConfig DesktopDefault() noexcept {
        return Desktop64K();
    }

    [[nodiscard]] static constexpr WorldConfig Mobile4K() noexcept {
        WorldConfig config;
        config.chunkSizeProfile = ChunkSizeProfile::Chunk4KB;
        config.reserveEntities = 256 * 1024;
        config.reserveArchetypes = 128;
        config.reserveQueryCache = 256;
        config.executionGrainSize = 512;
        config.queryPrefetchDistance = 8;
        config.workerThreadLimit = 2;
        return config;
    }

    [[nodiscard]] static constexpr WorldConfig Mobile8K() noexcept {
        WorldConfig config = Mobile4K();
        config.chunkSizeProfile = ChunkSizeProfile::Chunk8KB;
        config.reserveEntities = 512 * 1024;
        config.executionGrainSize = 1024;
        config.queryPrefetchDistance = 12;
        config.workerThreadLimit = 4;
        return config;
    }

    [[nodiscard]] static constexpr WorldConfig Mobile16K() noexcept {
        WorldConfig config = Mobile8K();
        config.chunkSizeProfile = ChunkSizeProfile::Chunk16KB;
        config.reserveEntities = 768 * 1024;
        config.executionGrainSize = 2048;
        config.queryPrefetchDistance = 16;
        config.workerThreadLimit = 4;
        return config;
    }

    [[nodiscard]] static constexpr WorldConfig Balanced32K() noexcept {
        WorldConfig config;
        config.chunkSizeProfile = ChunkSizeProfile::Chunk32KB;
        config.reserveEntities = 1024 * 1024;
        config.reserveArchetypes = 256;
        config.reserveQueryCache = 512;
        config.executionGrainSize = 4096;
        config.queryPrefetchDistance = 24;
        config.workerThreadLimit = 0;
        return config;
    }

    [[nodiscard]] static constexpr WorldConfig Desktop64K() noexcept {
        WorldConfig config = Balanced32K();
        config.chunkSizeProfile = ChunkSizeProfile::Chunk64KB;
        config.executionGrainSize = 4096;
        config.queryPrefetchDistance = 32;
        return config;
    }

    [[nodiscard]] static constexpr WorldConfig Streaming128KPlus() noexcept {
        WorldConfig config = Desktop64K();
        config.chunkSizeProfile = ChunkSizeProfile::Chunk128KB;
        config.reserveEntities = 2 * 1024 * 1024;
        config.reserveArchetypes = 512;
        config.reserveQueryCache = 1024;
        config.executionGrainSize = 8192;
        config.queryPrefetchDistance = 48;
        return config;
    }

    [[nodiscard]] static constexpr WorldConfig BenchmarkAuto() noexcept {
        WorldConfig config = Balanced32K();
        return config;
    }

    [[nodiscard]] static constexpr WorldConfig BenchmarkDefault() noexcept {
        return Balanced32K();
    }

    [[nodiscard]] static constexpr WorldConfig StressDefault() noexcept {
        WorldConfig config = Streaming128KPlus();
        config.reserveEntities = 5 * 1024 * 1024;
        config.reserveArchetypes = 512;
        config.reserveQueryCache = 1024;
        return config;
    }
};

} // namespace kb::ecs
