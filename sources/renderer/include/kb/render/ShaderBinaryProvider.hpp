#pragma once

#include <bgfx/bgfx.h>

#include <cstdint>
#include <string_view>
#include <vector>

namespace kb::render {

// Read-only shader source owned by the active bgfx renderer. A provider is
// exclusive: when installed, missing bytes are a package error and must never
// fall through to a developer filesystem.
class ShaderBinaryProvider {
public:
    virtual ~ShaderBinaryProvider() = default;

    [[nodiscard]] virtual bool ReadFixedShader(
        bgfx::RendererType::Enum renderer,
        std::string_view name,
        std::vector<std::uint8_t>& bytes,
        std::uint64_t& revision) const = 0;

    [[nodiscard]] virtual bool ReadMaterialShader(
        std::uint64_t graphSourceHash,
        std::uint64_t variantKey,
        std::string_view pass,
        bgfx::RendererType::Enum renderer,
        std::string_view stage,
        std::vector<std::uint8_t>& bytes,
        std::uint64_t& revision) const = 0;

    [[nodiscard]] virtual std::uint64_t MaterialShaderRevision(
        std::uint64_t graphSourceHash,
        std::uint64_t variantKey,
        std::string_view pass,
        bgfx::RendererType::Enum renderer) const noexcept = 0;
};

} // namespace kb::render
