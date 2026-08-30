#pragma once

#include <bgfx/bgfx.h>

#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace kb::render {

enum class ShaderStage : std::uint8_t {
    Vertex,
    Fragment,
    Compute,
};

struct ShaderManifestEntry {
    const char* name = "";
    ShaderStage stage = ShaderStage::Vertex;
    bool required = true;
};

struct ShaderProgramManifestEntry {
    const char* name = "";
    const char* vertexShader = "";
    const char* fragmentShader = "";
    bool required = true;
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
[[nodiscard]] ShaderManifestValidationResult ValidateShaderManifestProfile(const std::filesystem::path& profileRoot);

} // namespace kb::render
