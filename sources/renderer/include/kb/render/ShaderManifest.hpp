#pragma once

#include <bgfx/bgfx.h>

#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace kb::assets::bake {
enum class ShaderBakeBackend : std::uint8_t;
}

namespace kb::render {

enum class ShaderStage : std::uint8_t {
    Vertex,
    Fragment,
    Compute,
};

enum class ShaderRuntimeFeature : std::uint8_t {
    GpuDrivenCulling,
    ParticleGpuVisual,
    Editor,
};

using ShaderRuntimeFeatureMask = std::uint32_t;

[[nodiscard]] constexpr ShaderRuntimeFeatureMask ShaderRuntimeFeatureBit(
    ShaderRuntimeFeature feature) noexcept {
    return static_cast<ShaderRuntimeFeatureMask>(1U) << static_cast<std::uint32_t>(feature);
}

struct ShaderManifestEntry {
    const char* name = "";
    ShaderStage stage = ShaderStage::Vertex;
    bool required = true;
    // Core shaders leave this empty. Feature shaders are required only when
    // their bit is selected for the concrete target/backend being validated.
    ShaderRuntimeFeatureMask requiredFeature = 0U;
};

struct ShaderProgramManifestEntry {
    const char* name = "";
    const char* vertexShader = "";
    const char* fragmentShader = "";
    bool required = true;
    // Editor-only programs are excluded from packaged games unless their feature is selected.
    // Core runtime programs leave this empty, including skinned and motion-vector variants.
    ShaderRuntimeFeatureMask requiredFeature = 0U;
};

struct ShaderManifestValidationResult {
    std::filesystem::path profileRoot;
    std::vector<std::string> missingRequiredShaders;
    std::uint32_t checkedRequiredShaderCount = 0;

    [[nodiscard]] bool Succeeded() const noexcept {
        return missingRequiredShaders.empty();
    }
};

[[nodiscard]] const char* ShaderStageName(ShaderStage stage) noexcept;

// Leaf directory holding the shader binaries for `renderer`, e.g.
// "shaders/spirv". Returns NULLPTR for a backend we ship no bytecode for, and
// callers must treat that as "this renderer has no shaders", not substitute one.
//
// There is no fallback on purpose: a bgfx .bin header carries no backend
// identifier, so a backend handed another backend's blob does not fail to load,
// it takes a hard BGFX_FATAL. A wrong directory is therefore strictly worse than
// no directory.
[[nodiscard]] const char* ShaderProfileDirectoryForRenderer(bgfx::RendererType::Enum renderer) noexcept;
[[nodiscard]] std::span<const ShaderManifestEntry> RequiredShaderManifest() noexcept;
[[nodiscard]] std::span<const ShaderProgramManifestEntry> RequiredShaderProgramManifest() noexcept;
// Canonical closure used by both the cooker and release validator. Besides individually
// required shaders it includes both stages of every selected required program, even when the
// corresponding ShaderManifestEntry is marked optional for legacy prebuilt bundles.
[[nodiscard]] std::vector<std::string_view> RequiredPackagedShaderNames(
    ShaderRuntimeFeatureMask requiredFeatures = 0U);
[[nodiscard]] ShaderRuntimeFeatureMask PackagedGameShaderFeatures(
    kb::assets::bake::ShaderBakeBackend backend) noexcept;
[[nodiscard]] ShaderManifestValidationResult ValidateShaderManifestProfile(
    const std::filesystem::path& profileRoot,
    ShaderRuntimeFeatureMask requiredFeatures = 0U);
[[nodiscard]] ShaderManifestValidationResult ValidatePackagedShaderManifestProfile(
    const std::filesystem::path& profileRoot,
    ShaderRuntimeFeatureMask requiredFeatures = 0U);

} // namespace kb::render
