#include "engine/packaging/PackagingTargetCatalog.hpp"

#include <array>

namespace kb::packaging {
namespace {

constexpr std::array<PackagingTargetSpec, 6> kTargets{{
    { PackagingTarget::WindowsX64, "Windows.x64", "Windows 64 bit", "Player package | x86-64 | Portable folder", PackagingTargetFamily::Desktop, true, false },
    { PackagingTarget::AndroidAstcArm64, "Android.ASTC.arm64", "Android ASTC", "Player package | arm64-v8a | APK | ASTC", PackagingTargetFamily::Android, false, true },
    { PackagingTarget::AndroidEtc2Arm64, "Android.ETC2.arm64", "Android ETC2", "Player package | arm64-v8a | APK | ETC2", PackagingTargetFamily::Android, false, true },
    { PackagingTarget::LinuxX64, "Linux.x64", "Linux 64 bit", "Player package | x86-64 | Portable folder", PackagingTargetFamily::Desktop, true, false },
    { PackagingTarget::WebGlWasm32, "WebGL.wasm32", "WebGL", "Browser package | wasm32 | WebGL", PackagingTargetFamily::Web, true, false },
    { PackagingTarget::WebGpuWasm32, "WebGPU.wasm32", "WebGPU", "Browser package | wasm32 | WebGPU", PackagingTargetFamily::Web, true, false },
}};

} // namespace

std::span<const PackagingTargetSpec> PackagingTargets() noexcept {
    return kTargets;
}

const PackagingTargetSpec* FindPackagingTarget(std::string_view bakeId) noexcept {
    for (const PackagingTargetSpec& target : kTargets) {
        if (target.bakeId == bakeId) {
            return &target;
        }
    }
    return nullptr;
}

} // namespace kb::packaging
