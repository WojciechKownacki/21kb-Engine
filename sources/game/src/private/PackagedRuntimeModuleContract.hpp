#pragma once

#include "engine/project/ProjectDescriptor.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <string_view>

namespace kb::game {

// Stable identities of providers compiled into monolithic packaged hosts. Names live in the
// table below and nowhere in a platform host or cooker, so a newly supported provider changes
// cooking, validation and host construction through one reviewable contract.
enum class PackagedRuntimeModuleKind : std::uint8_t {
    PhysicsJolt,
    AudioMiniaudio,
    BasicLighting,
    Particle21kb,
};

struct PackagedRuntimeModuleDesc {
    PackagedRuntimeModuleKind kind{};
    std::string_view name;
};

inline constexpr std::array<PackagedRuntimeModuleDesc, 4U> kPackagedRuntimeModules{
    PackagedRuntimeModuleDesc{ PackagedRuntimeModuleKind::PhysicsJolt, "Physics.Jolt" },
    PackagedRuntimeModuleDesc{ PackagedRuntimeModuleKind::AudioMiniaudio, "Audio.Miniaudio" },
    PackagedRuntimeModuleDesc{ PackagedRuntimeModuleKind::BasicLighting, "Rendering.BasicLighting" },
    PackagedRuntimeModuleDesc{ PackagedRuntimeModuleKind::Particle21kb, "Rendering.21kbParticle" },
};

[[nodiscard]] constexpr std::optional<PackagedRuntimeModuleKind>
TryPackagedRuntimeModuleKind(std::string_view name) noexcept {
    for (const PackagedRuntimeModuleDesc& module : kPackagedRuntimeModules) {
        if (module.name == name) {
            return module.kind;
        }
    }
    return std::nullopt;
}

// Android packages are monolithic: their descriptor cannot name an arbitrary desktop DLL.
// Windows remains dynamically extensible. Linux/WebGL may opt into the same policy when their
// packaged hosts are introduced, without changing the module identities above.
[[nodiscard]] constexpr bool TargetRequiresPackagedRuntimeModules(
    std::string_view targetProfileId) noexcept {
    return targetProfileId == "Android.arm64";
}

[[nodiscard]] constexpr bool TargetSupportsPackagedRuntimeModule(
    std::string_view targetProfileId,
    std::string_view moduleName) noexcept {
    return !TargetRequiresPackagedRuntimeModules(targetProfileId) ||
        TryPackagedRuntimeModuleKind(moduleName).has_value();
}

// Returns the first enabled module the target host cannot construct. The returned view refers
// to the descriptor and remains valid while the descriptor does.
[[nodiscard]] inline std::optional<std::string_view> FirstUnsupportedPackagedRuntimeModule(
    std::string_view targetProfileId,
    const kb::project::ProjectDescriptor& descriptor) noexcept {
    for (const kb::project::ProjectPluginReference& plugin : descriptor.plugins) {
        if (plugin.enabled &&
            !TargetSupportsPackagedRuntimeModule(targetProfileId, plugin.name)) {
            return plugin.name;
        }
    }
    return std::nullopt;
}

} // namespace kb::game
