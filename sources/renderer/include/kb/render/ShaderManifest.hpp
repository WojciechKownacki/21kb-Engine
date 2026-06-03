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
[[nodiscard]] const char* ShaderProfileDirectoryForRenderer(bgfx::RendererType::Enum renderer) noexcept;
[[nodiscard]] std::span<const ShaderManifestEntry> RequiredShaderManifest() noexcept;
[[nodiscard]] std::span<const ShaderProgramManifestEntry> RequiredShaderProgramManifest() noexcept;
[[nodiscard]] ShaderManifestValidationResult ValidateShaderManifestProfile(const std::filesystem::path& profileRoot);

} // namespace kb::render
