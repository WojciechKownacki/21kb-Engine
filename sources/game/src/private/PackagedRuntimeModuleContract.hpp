#pragma once

#include "engine/project/ProjectDescriptor.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
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
    std::string_view windowsBinaryName;
};

inline constexpr std::array<PackagedRuntimeModuleDesc, 4U> kPackagedRuntimeModules{
    PackagedRuntimeModuleDesc{
        PackagedRuntimeModuleKind::PhysicsJolt, "Physics.Jolt", "kb_physics_jolt_plugin.dll" },
    PackagedRuntimeModuleDesc{
        PackagedRuntimeModuleKind::AudioMiniaudio, "Audio.Miniaudio", "kb_audio_miniaudio_plugin.dll" },
    PackagedRuntimeModuleDesc{
        PackagedRuntimeModuleKind::BasicLighting, "Rendering.BasicLighting", "kb_basic_lighting_plugin.dll" },
    PackagedRuntimeModuleDesc{
        PackagedRuntimeModuleKind::Particle21kb, "Rendering.21kbParticle", "kb_21kb_particle_plugin.dll" },
};

inline constexpr std::string_view kWindowsCustomRuntimeModuleDirectory = "RuntimeModules";

[[nodiscard]] constexpr const PackagedRuntimeModuleDesc*
FindPackagedRuntimeModule(std::string_view name) noexcept {
    for (const PackagedRuntimeModuleDesc& module : kPackagedRuntimeModules) {
        if (module.name == name) {
            return &module;
        }
    }
    return nullptr;
}

[[nodiscard]] constexpr std::optional<PackagedRuntimeModuleKind>
TryPackagedRuntimeModuleKind(std::string_view name) noexcept {
    if (const PackagedRuntimeModuleDesc* const module = FindPackagedRuntimeModule(name);
        module != nullptr) {
        return module->kind;
    }
    return std::nullopt;
}

// Both the cooker and the Windows package host use this exact lexical contract. Filesystem
// containment and existence are checked separately against their respective sealed roots.
[[nodiscard]] inline bool IsSafeWindowsRuntimeModuleRelativePath(
    const std::filesystem::path& path) {
    if (path.empty() || path.is_absolute() || path.has_root_name() || path.has_root_directory() ||
        path.filename().empty()) {
        return false;
    }
    for (const std::filesystem::path& component : path) {
        if (component == "." || component == "..") {
            return false;
        }
        const std::string text = component.string();
        if (text.empty() || text.back() == '.' || text.back() == ' ' ||
            std::ranges::any_of(text, [](unsigned char value) {
                return value < 32U || value == ':' || value == '<' || value == '>' ||
                    value == '"' || value == '|' || value == '?' || value == '*';
            })) {
            return false;
        }
        std::string deviceName = text.substr(0U, text.find('.'));
        std::ranges::transform(deviceName, deviceName.begin(), [](unsigned char value) {
            return static_cast<char>(std::toupper(value));
        });
        const bool numberedDevice = deviceName.size() == 4U &&
            (deviceName.starts_with("COM") || deviceName.starts_with("LPT")) &&
            deviceName.back() >= '1' && deviceName.back() <= '9';
        if (deviceName == "CON" || deviceName == "PRN" || deviceName == "AUX" ||
            deviceName == "NUL" || deviceName == "CONIN$" || deviceName == "CONOUT$" ||
            numberedDevice) {
            return false;
        }
    }
    std::string extension = path.extension().string();
    std::ranges::transform(extension, extension.begin(), [](unsigned char value) {
        return static_cast<char>(std::tolower(value));
    });
    return extension == ".dll";
}

// Packaged mobile, Linux and browser players are monolithic: their descriptor
// cannot name an arbitrary desktop DLL. Windows remains dynamically extensible.
[[nodiscard]] constexpr bool TargetRequiresPackagedRuntimeModules(
    std::string_view targetProfileId) noexcept {
    return targetProfileId.starts_with("Android.") || targetProfileId == "Linux.x64" ||
        targetProfileId.starts_with("WebGL.") || targetProfileId.starts_with("WebGPU.");
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
