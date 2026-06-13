#include "engine/modules/EngineModuleLoader.hpp"

#include <system_error>
#include <utility>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <dlfcn.h>
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

EngineModuleLoadResult EngineModuleLoader::Load(EngineModuleLoadDesc desc) {
    EngineModuleLoadResult result{};
    desc.key = NormalizeKey(std::move(desc.key), desc.modulePath);
    if (desc.modulePath.empty()) {
        result.errors.push_back(desc.diagnosticLabel + " path is empty");
        return result;
    }

    const std::filesystem::path sourcePath = std::filesystem::absolute(desc.modulePath);
    if (!std::filesystem::is_regular_file(sourcePath)) {
        result.errors.push_back(desc.diagnosticLabel + " file is missing: " + sourcePath.string());
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
