#pragma once

#include <bgfx/bgfx.h>

#include <cstdint>
#include <memory>
#include <string_view>
#include <vector>

namespace kb::render {

class ShaderBinaryProvider;

class ShaderLoader {
public:
    ShaderLoader() = delete;

    // bgfx itself permits only one active process-wide renderer. The active
    // Renderer installs its owned provider for exactly that lifetime. A
    // provider is exclusive: packaged runtime never falls back to loose files.
    static void SetBinaryProvider(std::shared_ptr<const ShaderBinaryProvider> provider) noexcept;
    static void ClearBinaryProvider(const std::shared_ptr<const ShaderBinaryProvider>& provider) noexcept;
    [[nodiscard]] static bool HasBinaryProvider() noexcept;

    [[nodiscard]] static bgfx::ShaderHandle Load(const char* name);
    [[nodiscard]] static bgfx::ProgramHandle LoadProgram(const char* vertexShader, const char* fragmentShader);
    [[nodiscard]] static bgfx::ProgramHandle LoadComputeProgram(const char* computeShader);
    [[nodiscard]] static bool ReadMaterialBinary(
        std::uint64_t graphSourceHash,
        std::uint64_t variantKey,
        std::string_view pass,
        std::uint32_t renderer,
        std::string_view stage,
        std::vector<std::uint8_t>& bytes,
        std::uint64_t& revision);
    [[nodiscard]] static std::uint64_t MaterialBinaryRevision(
        std::uint64_t graphSourceHash,
        std::uint64_t variantKey,
        std::string_view pass,
        std::uint32_t renderer) noexcept;
};

} // namespace kb::render
