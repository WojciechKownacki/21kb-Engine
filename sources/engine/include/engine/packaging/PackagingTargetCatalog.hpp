#pragma once

#include <cstdint>
#include <span>
#include <string_view>

namespace kb::packaging {

enum class PackagingTarget : std::uint8_t {
    WindowsX64,
    AndroidAstcArm64,
    AndroidEtc2Arm64,
    LinuxX64,
    WebGlWasm32,
    WebGpuWasm32,
};

enum class PackagingTargetFamily : std::uint8_t {
    Desktop,
    Android,
    Web,
};

struct PackagingTargetSpec {
    PackagingTarget target{};
    std::string_view bakeId;
    std::string_view displayName;
    std::string_view summary;
    PackagingTargetFamily family{};
    bool needsExecutableName = false;
    bool needsAndroidMetadata = false;
};

[[nodiscard]] std::span<const PackagingTargetSpec> PackagingTargets() noexcept;
[[nodiscard]] const PackagingTargetSpec* FindPackagingTarget(std::string_view bakeId) noexcept;

} // namespace kb::packaging
