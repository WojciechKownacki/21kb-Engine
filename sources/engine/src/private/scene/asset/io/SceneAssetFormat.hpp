#pragma once

#include <array>
#include <cstdint>
#include <string_view>

namespace kb::scene {

struct SceneAssetFormat {
    static constexpr std::array<std::uint8_t, 8U> Magic{ '2', '1', 'K', 'B', 'S', 'C', 'N', 0 };
    static constexpr std::array<std::uint8_t, 8U> MetaMagic{ '2', '1', 'K', 'B', 'S', 'M', 'T', 0 };
    static constexpr std::uint32_t BinaryVersion = 1U;
    static constexpr std::uint32_t MaxNodeCount = 1'000'000U;
    static constexpr std::uint32_t MaxDependencyCount = 1'000'000U;
    static constexpr std::uint32_t MaxNestedOverrideCount = 1'000'000U;
    static constexpr std::uint32_t MaxStringBytes = 1U << 20U;
    static constexpr std::string_view Extension = ".21kbscene";
};

} // namespace kb::scene
