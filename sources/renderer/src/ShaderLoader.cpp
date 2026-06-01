#include "kb/render/ShaderLoader.hpp"

#include <bgfx/bgfx.h>
#include <bx/string.h>

#include <fstream>
#include <string>
#include <vector>

#if defined(_WIN32)
#include <Windows.h>
#endif

namespace kb::render {
namespace {

[[nodiscard]] std::string ExecutableDirectory() {
#if defined(_WIN32)
    char path[MAX_PATH]{};
    const DWORD length = GetModuleFileNameA(nullptr, path, MAX_PATH);
    if (length == 0) {
        return {};
    }

    std::string fullPath(path, length);
    const std::size_t slash = fullPath.find_last_of("\\/");
    return slash == std::string::npos ? std::string{} : fullPath.substr(0, slash);
#else
    return {};
#endif
}

[[nodiscard]] std::string ParentDirectory(const std::string& path) {
    const std::size_t slash = path.find_last_of("\\/");
    return slash == std::string::npos ? std::string{} : path.substr(0, slash);
}

[[nodiscard]] std::vector<std::string> ShaderSearchRoots(const std::string& exeDir) {
    std::vector<std::string> roots;
    if (!exeDir.empty()) {
        roots.push_back(exeDir);

        const std::string configDir = ParentDirectory(exeDir);
        const std::string buildDir = ParentDirectory(configDir);
        if (!buildDir.empty()) {
            roots.push_back(buildDir);
        }
    }
    roots.emplace_back(".");
    return roots;
}

[[nodiscard]] const char* ShaderProfileDirectory() noexcept {
    switch (bgfx::getRendererType()) {
    case bgfx::RendererType::Noop:
    case bgfx::RendererType::Direct3D11:
        return "shaders/dxbc";
    case bgfx::RendererType::Direct3D12:
        return "shaders/dxil";
    case bgfx::RendererType::Vulkan:
        return "shaders/spirv";
    case bgfx::RendererType::OpenGL:
        return "shaders/glsl";
    case bgfx::RendererType::OpenGLES:
        return "shaders/essl";
    case bgfx::RendererType::Metal:
        return "shaders/metal";
    default:
        return "shaders/dxbc";
    }
}

[[nodiscard]] std::vector<std::uint8_t> ReadFileBytes(const char* path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        return {};
    }

    const std::streamsize size = file.tellg();
    if (size <= 0) {
        return {};
    }

    file.seekg(0, std::ios::beg);
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
    if (!file.read(reinterpret_cast<char*>(bytes.data()), size)) {
        return {};
    }

    return bytes;
}

} // namespace

bgfx::ShaderHandle ShaderLoader::Load(const char* name) {
    const std::string exeDir = ExecutableDirectory();
    std::vector<std::uint8_t> bytes;
    char resolvedPath[512]{};
    for (const std::string& root : ShaderSearchRoots(exeDir)) {
        char path[512]{};
        bx::snprintf(path, sizeof(path), "%s/%s/%s.bin", root.empty() ? "." : root.c_str(), ShaderProfileDirectory(), name);
        bytes = ReadFileBytes(path);
        if (!bytes.empty()) {
            bx::strCopy(resolvedPath, sizeof(resolvedPath), path);
            break;
        }
    }
    if (bytes.empty()) {
        return BGFX_INVALID_HANDLE;
    }

    const bgfx::Memory* memory = bgfx::copy(bytes.data(), static_cast<std::uint32_t>(bytes.size()));
    bgfx::ShaderHandle shader = bgfx::createShader(memory);
    bgfx::setName(shader, resolvedPath[0] == '\0' ? name : resolvedPath);
    return shader;
}

bgfx::ProgramHandle ShaderLoader::LoadProgram(const char* vertexShader, const char* fragmentShader) {
    bgfx::ShaderHandle vertex = Load(vertexShader);
    if (!bgfx::isValid(vertex)) {
        return BGFX_INVALID_HANDLE;
    }

    bgfx::ShaderHandle fragment = Load(fragmentShader);
    if (!bgfx::isValid(fragment)) {
        bgfx::destroy(vertex);
        return BGFX_INVALID_HANDLE;
    }

    return bgfx::createProgram(vertex, fragment, true);
}

} // namespace kb::render
