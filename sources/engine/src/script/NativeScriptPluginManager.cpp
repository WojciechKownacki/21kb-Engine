#include "engine/script/NativeScriptPluginManager.hpp"

#include "engine/script/NativeScriptPlugin.hpp"
#include "engine/script/NativeScriptPluginRegistrar.hpp"

#include <algorithm>
#include <chrono>
#include <system_error>
#include <utility>

#if defined(_WIN32)
#define NOMINMAX
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace kb::script {
namespace {

[[nodiscard]] std::string SanitizeFileName(std::string_view value) {
    std::string output;
    output.reserve(value.size());
    for (const char character : value) {
        const bool valid = (character >= 'A' && character <= 'Z') || (character >= 'a' && character <= 'z') || (character >= '0' && character <= '9') || character == '_' || character == '-';
        output.push_back(valid ? character : '_');
    }
    return output.empty() ? "native_plugin" : output;
}

[[nodiscard]] std::string NativeLibraryError() {
#if defined(_WIN32)
    return "native script plugin loader error " + std::to_string(static_cast<unsigned long>(GetLastError()));
#else
    const char* error = dlerror();
    return error == nullptr ? "native script plugin loader error" : std::string{ error };
#endif
}

} // namespace

NativeScriptPluginManager::NativeScriptPluginManager(NativeScriptBackend& backend) noexcept
    : backend_(backend) {}

NativeScriptPluginManager::~NativeScriptPluginManager() {
    Clear();
}

NativeScriptPluginLoadResult NativeScriptPluginManager::LoadOrReload(NativeScriptPluginLoadDesc desc) {
    NativeScriptPluginLoadResult result{};
    desc.key = NormalizeKey(std::move(desc.key), desc.modulePath);
    if (desc.modulePath.empty()) {
        result.errors.push_back("native script plugin module path is empty");
        return result;
    }
    if (desc.entryPoint.empty()) {
        desc.entryPoint = kNativeScriptPluginDefaultEntryPoint;
    }

    const std::filesystem::path sourcePath = std::filesystem::absolute(desc.modulePath);
    if (!std::filesystem::is_regular_file(sourcePath)) {
        result.errors.push_back("native script plugin module file is missing: " + sourcePath.string());
        return result;
    }

    std::filesystem::path loadPath = sourcePath;
    if (desc.shadowCopy) {
        loadPath = ResolveShadowCopyPath(desc.key, sourcePath, desc.shadowCopyDirectory);
        if (!CopyForLoad(sourcePath, loadPath, result.errors)) {
            return result;
        }
    }

    void* library = LoadNativeLibrary(loadPath, result.errors);
    if (library == nullptr) {
        return result;
    }

    std::optional<LoadedPlugin> previous = Detach(desc.key);
    if (previous.has_value()) {
        for (const std::string& registeredSymbol : previous->registeredSymbols) {
            backend_.UnregisterSymbolCallbacks(registeredSymbol);
        }
    }

    std::vector<std::string> registeredSymbols;
    if (!RegisterPluginSymbols(library, desc.entryPoint, registeredSymbols, result.errors)) {
        CloseNativeLibrary(library);
        if (previous.has_value()) {
            RestoreOrClose(std::move(*previous), result.errors);
        }
        return result;
    }

    if (previous.has_value()) {
        CloseNativeLibrary(previous->library);
    }

    result.loaded = true;
    result.loadedPath = loadPath;
    result.registeredSymbols = registeredSymbols;
    plugins_.push_back(LoadedPlugin{
        .key = desc.key,
        .originalPath = sourcePath,
        .loadedPath = loadPath,
        .entryPoint = desc.entryPoint,
        .library = library,
        .registeredSymbols = result.registeredSymbols,
    });
    return result;
}

bool NativeScriptPluginManager::IsLoaded(std::string_view key) const noexcept {
    return std::ranges::find_if(plugins_, [key](const LoadedPlugin& plugin) {
        return plugin.key == key;
    }) != plugins_.end();
}

void NativeScriptPluginManager::Unload(std::string_view key) noexcept {
    while (std::optional<LoadedPlugin> plugin = Detach(key)) {
        for (const std::string& symbol : plugin->registeredSymbols) {
            backend_.UnregisterSymbol(symbol);
        }
        CloseNativeLibrary(plugin->library);
    }
}

void NativeScriptPluginManager::Clear() noexcept {
    for (LoadedPlugin& plugin : plugins_) {
        for (const std::string& symbol : plugin.registeredSymbols) {
            backend_.UnregisterSymbol(symbol);
        }
        CloseNativeLibrary(plugin.library);
    }
    plugins_.clear();
}

std::string NativeScriptPluginManager::NormalizeKey(std::string key, const std::filesystem::path& modulePath) {
    if (!key.empty()) {
        return key;
    }
    return std::filesystem::absolute(modulePath).string();
}

std::filesystem::path NativeScriptPluginManager::ResolveShadowCopyPath(
    std::string_view key,
    const std::filesystem::path& modulePath,
    const std::filesystem::path& shadowCopyDirectory) {
    std::filesystem::path directory = shadowCopyDirectory;
    if (directory.empty()) {
        directory = std::filesystem::temp_directory_path() / "21kb_native_script_plugins";
    }
    const std::string extension = modulePath.extension().string();
    return directory / (SanitizeFileName(key) + "_" + std::to_string(reloadSerial_++) + extension);
}

bool NativeScriptPluginManager::CopyForLoad(
    const std::filesystem::path& source,
    const std::filesystem::path& destination,
    std::vector<std::string>& errors) {
    std::error_code error;
    std::filesystem::create_directories(destination.parent_path(), error);
    if (error) {
        errors.push_back("native script plugin shadow copy directory could not be created");
        return false;
    }
    std::filesystem::copy_file(source, destination, std::filesystem::copy_options::overwrite_existing, error);
    if (error) {
        errors.push_back("native script plugin shadow copy failed");
        return false;
    }
    return true;
}

void* NativeScriptPluginManager::LoadNativeLibrary(const std::filesystem::path& path, std::vector<std::string>& errors) {
#if defined(_WIN32)
    HMODULE library = LoadLibraryW(path.wstring().c_str());
    if (library == nullptr) {
        errors.push_back(NativeLibraryError());
    }
    return reinterpret_cast<void*>(library);
#else
    void* library = dlopen(path.string().c_str(), RTLD_NOW | RTLD_LOCAL);
    if (library == nullptr) {
        errors.push_back(NativeLibraryError());
    }
    return library;
#endif
}

void* NativeScriptPluginManager::FindNativeSymbol(void* library, const std::string& name, std::vector<std::string>& errors) {
    if (library == nullptr || name.empty()) {
        errors.push_back("native script plugin symbol lookup request is invalid");
        return nullptr;
    }
#if defined(_WIN32)
    FARPROC symbol = GetProcAddress(reinterpret_cast<HMODULE>(library), name.c_str());
    if (symbol == nullptr) {
        errors.push_back(NativeLibraryError());
    }
    return reinterpret_cast<void*>(symbol);
#else
    void* symbol = dlsym(library, name.c_str());
    if (symbol == nullptr) {
        errors.push_back(NativeLibraryError());
    }
    return symbol;
#endif
}

void NativeScriptPluginManager::CloseNativeLibrary(void* library) noexcept {
    if (library == nullptr) {
        return;
    }
#if defined(_WIN32)
    FreeLibrary(reinterpret_cast<HMODULE>(library));
#else
    dlclose(library);
#endif
}

std::optional<NativeScriptPluginManager::LoadedPlugin> NativeScriptPluginManager::Detach(std::string_view key) noexcept {
    const auto iter = std::ranges::find_if(plugins_, [key](const LoadedPlugin& plugin) {
        return plugin.key == key;
    });
    if (iter == plugins_.end()) {
        return std::nullopt;
    }
    LoadedPlugin plugin = std::move(*iter);
    plugins_.erase(iter);
    return plugin;
}

bool NativeScriptPluginManager::RegisterPluginSymbols(
    void* library,
    const std::string& entryPoint,
    std::vector<std::string>& registeredSymbols,
    std::vector<std::string>& errors) {
    void* symbol = FindNativeSymbol(library, entryPoint, errors);
    if (symbol == nullptr) {
        return false;
    }

    auto registerPlugin = reinterpret_cast<NativeScriptPluginRegisterProc>(symbol);
    NativeScriptPluginRegistrar registrar{ backend_ };
    NativeScriptPluginApi api = registrar.CreateApi();
    if (!registerPlugin(&api)) {
        errors.push_back("native script plugin registration function returned false");
    }
    if (!registrar.Errors().empty()) {
        errors.insert(errors.end(), registrar.Errors().begin(), registrar.Errors().end());
    }
    if (!errors.empty()) {
        for (const std::string& registeredSymbol : registrar.RegisteredSymbols()) {
            backend_.UnregisterSymbolCallbacks(registeredSymbol);
        }
        return false;
    }

    registeredSymbols = registrar.RegisteredSymbols();
    return true;
}

void NativeScriptPluginManager::RestoreOrClose(LoadedPlugin plugin, std::vector<std::string>& errors) {
    std::vector<std::string> restoreErrors;
    std::vector<std::string> restoredSymbols;
    if (RegisterPluginSymbols(plugin.library, plugin.entryPoint, restoredSymbols, restoreErrors)) {
        plugin.registeredSymbols = std::move(restoredSymbols);
        plugins_.push_back(std::move(plugin));
        return;
    }

    errors.push_back("native script plugin rollback failed; previous plugin was unloaded");
    errors.insert(errors.end(), restoreErrors.begin(), restoreErrors.end());
    CloseNativeLibrary(plugin.library);
}

} // namespace kb::script
