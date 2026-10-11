#include "kb/render/resources/RenderMaterialGraphShaderToolchain.hpp"

#include <bgfx/bgfx.h>

#include <array>
#include <cstdlib>
#include <filesystem>
#include <system_error>
#include <utility>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace kb::render {
namespace {

[[nodiscard]] std::filesystem::path ExecutableDirectory() {
#if defined(_WIN32)
    std::wstring buffer(1024U, L'\0');
    const DWORD length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (length == 0U || length >= buffer.size()) {
        return {};
    }
    buffer.resize(length);
    return std::filesystem::path{ buffer }.parent_path();
#elif defined(__linux__)
    std::error_code error;
    const std::filesystem::path executable = std::filesystem::read_symlink("/proc/self/exe", error);
    return error ? std::filesystem::path{} : executable.parent_path();
#else
    return {};
#endif
}

[[nodiscard]] std::string ReadEnv(const char* name) {
#if defined(_WIN32)
    char* buffer = nullptr;
    std::size_t size = 0U;
    if (_dupenv_s(&buffer, &size, name) != 0 || buffer == nullptr) {
        return {};
    }
    std::string value{ buffer };
    std::free(buffer);
    return value;
#else
    const char* value = std::getenv(name);
    return value != nullptr ? std::string{ value } : std::string{};
#endif
}

[[nodiscard]] bool Exists(const std::string& path) {
    std::error_code error;
    return !path.empty() && std::filesystem::exists(path, error);
}

// bgfx's shaderc beside the running executable or in a build tree above it, so a normal build
// cooks graphs without KB_GRAPH_SHADERC set by hand.
[[nodiscard]] std::string DiscoverShadercNearExecutable() {
    std::filesystem::path dir = ExecutableDirectory();
    static constexpr std::array<const char*, 4U> kConfigs{ "Debug", "Release", "RelWithDebInfo", "MinSizeRel" };
    std::error_code error;
    for (int level = 0; level < 8 && !dir.empty(); ++level) {
        for (const char* exe : { "shaderc.exe", "shaderc" }) {
            const std::filesystem::path candidate = dir / exe;
            if (std::filesystem::exists(candidate, error)) {
                return candidate.string();
            }
        }
        // Single-configuration builds (Ninja, Makefiles) put shaderc straight in the bgfx build
        // directory; multi-configuration ones (Visual Studio) in a per-configuration directory.
        const std::filesystem::path bgfxBuild = dir / "third_party" / "bgfx.cmake" / "cmake" / "bgfx";
        for (const char* exe : { "shaderc.exe", "shaderc" }) {
            if (std::filesystem::exists(bgfxBuild / exe, error)) {
                return (bgfxBuild / exe).string();
            }
        }
        for (const char* config : kConfigs) {
            for (const char* exe : { "shaderc.exe", "shaderc" }) {
                const std::filesystem::path candidate = bgfxBuild / config / exe;
                if (std::filesystem::exists(candidate, error)) {
                    return candidate.string();
                }
            }
        }
        const std::filesystem::path parent = dir.parent_path();
        if (parent == dir) {
            break;
        }
        dir = parent;
    }
    return {};
}

// The engine source root (it holds sources/renderer/shaders/varying.def.sc), found by walking up
// from the running executable.
[[nodiscard]] std::filesystem::path DiscoverEngineShaderRoot() {
    std::filesystem::path dir = ExecutableDirectory();
    std::error_code error;
    for (int level = 0; level < 10 && !dir.empty(); ++level) {
        if (std::filesystem::exists(dir / "sources" / "renderer" / "shaders" / "varying.def.sc", error)) {
            return dir;
        }
        const std::filesystem::path parent = dir.parent_path();
        if (parent == dir) {
            break;
        }
        dir = parent;
    }
    return {};
}

} // namespace

RenderMaterialGraphShaderToolchain ResolveRenderMaterialGraphShaderToolchain(
    const RenderMaterialGraphShaderToolchain& builtIn) {
    RenderMaterialGraphShaderToolchain toolchain{};
#if defined(_WIN32)
    toolchain.shaderPlatform = kb::assets::bake::ShaderBakePlatform::Windows;
#elif defined(__ANDROID__)
    toolchain.shaderPlatform = kb::assets::bake::ShaderBakePlatform::Android;
#elif defined(__EMSCRIPTEN__)
    toolchain.shaderPlatform = kb::assets::bake::ShaderBakePlatform::WebGl;
#else
    toolchain.shaderPlatform = kb::assets::bake::ShaderBakePlatform::Linux;
#endif

    std::string shaderc = ReadEnv("KB_GRAPH_SHADERC");
    if (!Exists(shaderc)) {
        shaderc = builtIn.shadercPath;
    }
    if (!Exists(shaderc)) {
        shaderc = DiscoverShadercNearExecutable();
    }
    if (Exists(shaderc)) {
        toolchain.shadercPath = std::move(shaderc);
    }

    if (Exists(builtIn.varyingDefPath)) {
        toolchain.varyingDefPath = builtIn.varyingDefPath;
        toolchain.includeDirs = builtIn.includeDirs;
    } else if (const std::filesystem::path engineRoot = DiscoverEngineShaderRoot(); !engineRoot.empty()) {
        toolchain.varyingDefPath = (engineRoot / "sources" / "renderer" / "shaders" / "varying.def.sc").string();
        toolchain.includeDirs = {
            (engineRoot / "sources" / "renderer" / "shaders").string(),
            (engineRoot / "third_party" / "bgfx.cmake" / "bgfx" / "src").string(),
        };
    }
    return toolchain;
}

RenderMaterialGraphShaderBackend RenderMaterialGraphShaderBackendForActiveRenderer() noexcept {
    switch (bgfx::getRendererType()) {
    case bgfx::RendererType::Direct3D12:
        return RenderMaterialGraphShaderBackend::Dxil;
    case bgfx::RendererType::Vulkan:
        return RenderMaterialGraphShaderBackend::Spirv;
    case bgfx::RendererType::OpenGL:
        return RenderMaterialGraphShaderBackend::Glsl;
    case bgfx::RendererType::OpenGLES:
        return RenderMaterialGraphShaderBackend::Essl;
    case bgfx::RendererType::Metal:
        return RenderMaterialGraphShaderBackend::Metal;
    case bgfx::RendererType::Noop:
    case bgfx::RendererType::Direct3D11:
    default:
        return RenderMaterialGraphShaderBackend::Dxbc;
    }
}

} // namespace kb::render
