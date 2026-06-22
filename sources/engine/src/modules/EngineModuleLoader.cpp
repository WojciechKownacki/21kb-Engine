#include "engine/modules/EngineModuleLoader.hpp"

#include <algorithm>
#include <ranges>
#include <system_error>
#include <utility>
#include <vector>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <dlfcn.h>
#include <unistd.h>
#endif

namespace kb::modules {
namespace {

[[nodiscard]] std::string SanitizeFileName(std::string_view value) {
    std::string output;
    output.reserve(value.size());
    for (const char character : value) {
        const bool valid = (character >= 'A' && character <= 'Z') || (character >= 'a' && character <= 'z') || (character >= '0' && character <= '9') || character == '_' || character == '-';
        output.push_back(valid ? character : '_');
    }
    return output.empty() ? "engine_module" : output;
}

[[nodiscard]] std::string NativeLibraryError(std::string_view diagnosticLabel) {
#if defined(_WIN32)
    return std::string{ diagnosticLabel } + " loader error " + std::to_string(static_cast<unsigned long>(GetLastError()));
#else
    const char* error = dlerror();
    return error == nullptr ? std::string{ diagnosticLabel } + " loader error" : std::string{ error };
#endif
}

void CloseNativeLibrary(void* library) noexcept {
    if (library == nullptr) {
        return;
    }
#if defined(_WIN32)
    FreeLibrary(reinterpret_cast<HMODULE>(library));
#else
    dlclose(library);
#endif
}

[[nodiscard]] std::filesystem::path ExecutablePath() {
#if defined(_WIN32)
    wchar_t path[MAX_PATH]{};
    const DWORD length = GetModuleFileNameW(nullptr, path, MAX_PATH);
    return length == 0U ? std::filesystem::path{} : std::filesystem::path{ std::wstring(path, length) };
#else
    std::vector<char> buffer(1024, '\0');
    const ssize_t length = readlink("/proc/self/exe", buffer.data(), buffer.size() - 1U);
    return length <= 0 ? std::filesystem::path{} : std::filesystem::path{ std::string(buffer.data(), static_cast<std::size_t>(length)) };
#endif
}

[[nodiscard]] bool IsRegularFile(const std::filesystem::path& path) {
    std::error_code error;
    return !path.empty() && std::filesystem::is_regular_file(path, error) && !error;
}

void AppendCandidate(std::vector<std::filesystem::path>& candidates, const std::filesystem::path& candidate) {
    if (candidate.empty()) {
        return;
    }
    if (std::ranges::find(candidates, candidate) == candidates.end()) {
        candidates.push_back(candidate);
    }
}

} // namespace

EngineModuleLibrary::EngineModuleLibrary(std::filesystem::path originalPath, std::filesystem::path loadedPath, void* handle) noexcept
    : originalPath_(std::move(originalPath))
    , loadedPath_(std::move(loadedPath))
    , handle_(handle) {}

EngineModuleLibrary::~EngineModuleLibrary() {
    Reset();
}

EngineModuleLibrary::EngineModuleLibrary(EngineModuleLibrary&& other) noexcept
    : originalPath_(std::move(other.originalPath_))
    , loadedPath_(std::move(other.loadedPath_))
    , handle_(std::exchange(other.handle_, nullptr)) {}

EngineModuleLibrary& EngineModuleLibrary::operator=(EngineModuleLibrary&& other) noexcept {
    if (this == &other) {
        return *this;
    }
    Reset();
    originalPath_ = std::move(other.originalPath_);
    loadedPath_ = std::move(other.loadedPath_);
    handle_ = std::exchange(other.handle_, nullptr);
    return *this;
}

void* EngineModuleLibrary::FindSymbol(std::string_view name, std::vector<std::string>& errors, std::string_view diagnosticLabel) const {
    if (handle_ == nullptr || name.empty()) {
        errors.push_back(std::string{ diagnosticLabel } + " symbol lookup request is invalid");
        return nullptr;
    }
    const std::string symbolName{ name };
#if defined(_WIN32)
    FARPROC symbol = GetProcAddress(reinterpret_cast<HMODULE>(handle_), symbolName.c_str());
    if (symbol == nullptr) {
        errors.push_back(NativeLibraryError(diagnosticLabel));
    }
    return reinterpret_cast<void*>(symbol);
#else
    void* symbol = dlsym(handle_, symbolName.c_str());
    if (symbol == nullptr) {
        errors.push_back(NativeLibraryError(diagnosticLabel));
    }
    return symbol;
#endif
}

void EngineModuleLibrary::Reset() noexcept {
    CloseNativeLibrary(handle_);
    handle_ = nullptr;
    originalPath_.clear();
    loadedPath_.clear();
}

void EngineModuleLibrary::ReleaseWithoutUnload() noexcept {
    handle_ = nullptr;
    originalPath_.clear();
    loadedPath_.clear();
}

EngineModuleLoadResult EngineModuleLoader::Load(EngineModuleLoadDesc desc) {
    EngineModuleLoadResult result{};
    desc.key = NormalizeKey(std::move(desc.key), desc.modulePath);
    if (desc.modulePath.empty()) {
        result.errors.push_back(desc.diagnosticLabel + " path is empty");
        return result;
    }

    const std::filesystem::path sourcePath = ResolveModulePath(desc.modulePath);
    if (!IsRegularFile(sourcePath)) {
        result.errors.push_back(desc.diagnosticLabel + " file is missing: " + desc.modulePath.string());
        return result;
    }

    std::uint64_t serial = 0U;
    std::filesystem::path loadPath = sourcePath;
    if (desc.shadowCopy) {
        loadPath = ResolveShadowCopyPath(desc.key, sourcePath, desc.shadowCopyDirectory, desc.shadowCopyDirectoryName, serial);
        if (!CopyForLoad(sourcePath, loadPath, desc.diagnosticLabel, result.errors)) {
            return result;
        }
    }

    void* library = LoadNativeLibrary(loadPath, desc.diagnosticLabel, result.errors);
    if (library == nullptr) {
        return result;
    }

    result.loaded = true;
    result.originalPath = sourcePath;
    result.loadedPath = loadPath;
    result.reloadSerial = serial;
    result.library = EngineModuleLibrary{ sourcePath, loadPath, library };
    return result;
}

std::string EngineModuleLoader::NormalizeKey(std::string key, const std::filesystem::path& modulePath) {
    if (!key.empty()) {
        return key;
    }
    return std::filesystem::absolute(modulePath).string();
}

std::filesystem::path EngineModuleLoader::ResolveModulePath(const std::filesystem::path& modulePath) {
    if (modulePath.empty()) {
        return {};
    }

    std::vector<std::filesystem::path> candidates;
    if (modulePath.is_absolute()) {
        AppendCandidate(candidates, modulePath);
    } else {
        std::error_code error;
        AppendCandidate(candidates, std::filesystem::absolute(modulePath, error));

        const std::filesystem::path exePath = ExecutablePath();
        const std::filesystem::path exeDir = exePath.parent_path();
        AppendCandidate(candidates, exeDir / modulePath);
        AppendCandidate(candidates, exeDir.parent_path() / modulePath);
        AppendCandidate(candidates, exeDir.parent_path().parent_path() / modulePath);

        if (modulePath.filename() == modulePath) {
            const std::filesystem::path buildRoot = exeDir.parent_path().parent_path();
            const std::filesystem::path configDir = exeDir.filename();
            if (!buildRoot.empty() && !configDir.empty()) {
                std::error_code iterError;
                for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator(buildRoot, iterError)) {
                    if (iterError || !entry.is_directory()) {
                        continue;
                    }
                    AppendCandidate(candidates, entry.path() / configDir / modulePath.filename());
                }
            }
        }
    }

    for (const std::filesystem::path& candidate : candidates) {
        if (IsRegularFile(candidate)) {
            std::error_code error;
            const std::filesystem::path absolute = std::filesystem::absolute(candidate, error);
            return error ? candidate : absolute;
        }
    }

    return modulePath.is_absolute() ? modulePath : std::filesystem::absolute(modulePath);
}

std::filesystem::path EngineModuleLoader::ResolveShadowCopyPath(
    std::string_view key,
    const std::filesystem::path& modulePath,
    const std::filesystem::path& shadowCopyDirectory,
    std::string_view shadowCopyDirectoryName,
    std::uint64_t& serial) {
    std::filesystem::path directory = shadowCopyDirectory;
    if (directory.empty()) {
        directory = std::filesystem::temp_directory_path() / std::string{ shadowCopyDirectoryName.empty() ? "21kb_engine_modules" : shadowCopyDirectoryName };
    }
    const std::string extension = modulePath.extension().string();
    serial = reloadSerial_++;
    return directory / (SanitizeFileName(key) + "_" + std::to_string(serial) + extension);
}

bool EngineModuleLoader::CopyForLoad(
    const std::filesystem::path& source,
    const std::filesystem::path& destination,
    std::string_view diagnosticLabel,
    std::vector<std::string>& errors) {
    std::error_code error;
    std::filesystem::create_directories(destination.parent_path(), error);
    if (error) {
        errors.push_back(std::string{ diagnosticLabel } + " shadow copy directory could not be created");
        return false;
    }
    std::filesystem::copy_file(source, destination, std::filesystem::copy_options::overwrite_existing, error);
    if (error) {
        errors.push_back(std::string{ diagnosticLabel } + " shadow copy failed");
        return false;
    }
    return true;
}

void* EngineModuleLoader::LoadNativeLibrary(
    const std::filesystem::path& path,
    std::string_view diagnosticLabel,
    std::vector<std::string>& errors) {
#if defined(_WIN32)
    HMODULE library = LoadLibraryW(path.wstring().c_str());
    if (library == nullptr) {
        errors.push_back(NativeLibraryError(diagnosticLabel));
    }
    return reinterpret_cast<void*>(library);
#else
    void* library = dlopen(path.string().c_str(), RTLD_NOW | RTLD_LOCAL);
    if (library == nullptr) {
        errors.push_back(NativeLibraryError(diagnosticLabel));
    }
    return library;
#endif
}

} // namespace kb::modules
