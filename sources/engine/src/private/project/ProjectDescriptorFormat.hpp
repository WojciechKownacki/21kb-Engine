#pragma once

#include <array>
#include <cstdint>
#include <string_view>

namespace kb::project {

struct ProjectDescriptorFormat {
    static constexpr std::array<std::uint8_t, 8U> Magic{ '2', '1', 'K', 'B', 'P', 'R', 'J', 0 };
    static constexpr std::array<std::uint8_t, 8U> MetaMagic{ '2', '1', 'K', 'B', 'P', 'M', 'T', 0 };
    static constexpr std::string_view Extension = ".21kbproject";
    static constexpr std::uint32_t MaxStringBytes = 1U << 20U;
    static constexpr std::uint32_t MaxTargetPlatformCount = 256U;
    static constexpr std::uint32_t MaxModuleCount = 4096U;
    static constexpr std::uint32_t MaxPluginCount = 4096U;
};

} // namespace kb::project
