#pragma once

#include "engine/ecs/WorldConfig.hpp"

namespace kb::ecs {

class WorldConfigPresets {
public:
    [[nodiscard]] static constexpr WorldConfig DesktopDefault() noexcept {
        WorldConfig config;
        config.reserveEntities = 1024 * 1024;
        config.reserveArchetypes = 256;
        config.reserveQueryCache = 512;
        return config;
    }

    [[nodiscard]] static constexpr WorldConfig BenchmarkDefault() noexcept {
        return DesktopDefault();
    }

    [[nodiscard]] static constexpr WorldConfig StressDefault() noexcept {
        WorldConfig config = BenchmarkDefault();
        config.reserveEntities = 5 * 1024 * 1024;
        config.reserveArchetypes = 512;
        config.reserveQueryCache = 1024;
        return config;
    }
};

} // namespace kb::ecs
