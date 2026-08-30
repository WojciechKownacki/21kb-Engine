#include "kb/render/ShaderLoader.hpp"

#include "kb/render/ShaderBinaryProvider.hpp"
#include "kb/render/ShaderManifest.hpp"

#include <bgfx/bgfx.h>
#include <bx/string.h>

#include <fstream>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#if defined(_WIN32)
#include <Windows.h>
#endif

namespace kb::render {
namespace {

std::mutex gProviderMutex;
std::shared_ptr<const ShaderBinaryProvider> gProvider;

[[nodiscard]] std::shared_ptr<const ShaderBinaryProvider> ActiveProvider() noexcept {
    std::scoped_lock lock{ gProviderMutex };
    return gProvider;
}

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

[[nodiscard]] std::vector<std::uint8_t> ReadFileBytes(const char* path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (file.is_open()) {
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

    return {};
}

} // namespace

void ShaderLoader::SetBinaryProvider(std::shared_ptr<const ShaderBinaryProvider> provider) noexcept {
    std::scoped_lock lock{ gProviderMutex };
    gProvider = std::move(provider);
}

void ShaderLoader::ClearBinaryProvider(const std::shared_ptr<const ShaderBinaryProvider>& provider) noexcept {
    std::scoped_lock lock{ gProviderMutex };
    if (gProvider == provider) {
        gProvider.reset();
    }
}

bool ShaderLoader::HasBinaryProvider() noexcept {
    return ActiveProvider() != nullptr;
}

bgfx::ShaderHandle ShaderLoader::Load(const char* name) {
    // No profile directory means we ship no binaries for the renderer that came
    // up. Fail here rather than search some other backend's directory: bgfx's
    // .bin header has no backend identifier, so a mismatched blob is a hard
    // BGFX_FATAL, not a load error the caller could recover from.
    const char* profileDirectory = ShaderProfileDirectoryForRenderer(bgfx::getRendererType());
    if (profileDirectory == nullptr) {
        return BGFX_INVALID_HANDLE;
    }

    std::vector<std::uint8_t> bytes;
    char resolvedPath[512]{};
    if (const std::shared_ptr<const ShaderBinaryProvider> provider = ActiveProvider(); provider != nullptr) {
        std::uint64_t revision = 0U;
        if (!provider->ReadFixedShader(bgfx::getRendererType(), name, bytes, revision) || bytes.empty()) {
            return BGFX_INVALID_HANDLE;
        }
        bx::snprintf(resolvedPath, sizeof(resolvedPath), "package:%s/%s", profileDirectory, name);
    } else {
        const std::string exeDir = ExecutableDirectory();
        for (const std::string& root : ShaderSearchRoots(exeDir)) {
            char path[512]{};
            if (root.empty() || root == ".") {
                bx::snprintf(path, sizeof(path), "%s/%s.bin", profileDirectory, name);
            } else {
                bx::snprintf(path, sizeof(path), "%s/%s/%s.bin", root.c_str(), profileDirectory, name);
            }
            bytes = ReadFileBytes(path);
            if (!bytes.empty()) {
                bx::strCopy(resolvedPath, sizeof(resolvedPath), path);
                break;
            }
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

bgfx::ProgramHandle ShaderLoader::LoadComputeProgram(const char* computeShader) {
    bgfx::ShaderHandle compute = Load(computeShader);
    if (!bgfx::isValid(compute)) {
        return BGFX_INVALID_HANDLE;
    }

    return bgfx::createProgram(compute, true);
}

bool ShaderLoader::ReadMaterialBinary(
    std::uint64_t graphSourceHash,
    std::uint64_t variantKey,
    std::string_view pass,
    std::uint32_t renderer,
    std::string_view stage,
    std::vector<std::uint8_t>& bytes,
    std::uint64_t& revision) {
    const std::shared_ptr<const ShaderBinaryProvider> provider = ActiveProvider();
    return provider != nullptr && renderer < static_cast<std::uint32_t>(bgfx::RendererType::Count) &&
        provider->ReadMaterialShader(
            graphSourceHash,
            variantKey,
            pass,
            static_cast<bgfx::RendererType::Enum>(renderer),
            stage,
            bytes,
            revision);
}

std::uint64_t ShaderLoader::MaterialBinaryRevision(
    std::uint64_t graphSourceHash,
    std::uint64_t variantKey,
    std::string_view pass,
    std::uint32_t renderer) noexcept {
    const std::shared_ptr<const ShaderBinaryProvider> provider = ActiveProvider();
    return provider != nullptr && renderer < static_cast<std::uint32_t>(bgfx::RendererType::Count)
        ? provider->MaterialShaderRevision(
              graphSourceHash,
              variantKey,
              pass,
              static_cast<bgfx::RendererType::Enum>(renderer))
        : 0U;
}

} // namespace kb::render
