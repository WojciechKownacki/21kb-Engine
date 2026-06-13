#include "engine/script/NativeScriptPluginManager.hpp"

#include "engine/script/NativeScriptPlugin.hpp"
#include "engine/script/NativeScriptPluginRegistrar.hpp"

#include <algorithm>
#include <utility>

namespace kb::script {

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

    kb::modules::EngineModuleLoadResult loadedModule = loader_.Load(kb::modules::EngineModuleLoadDesc{
        .key = desc.key,
        .modulePath = desc.modulePath,
        .shadowCopy = desc.shadowCopy,
        .shadowCopyDirectory = desc.shadowCopyDirectory,
        .shadowCopyDirectoryName = "21kb_native_script_plugins",
        .diagnosticLabel = "native script plugin",
    });
    if (!loadedModule.Succeeded()) {
        result.errors = std::move(loadedModule.errors);
        return result;
    }

    std::optional<LoadedPlugin> previous = Detach(desc.key);
    if (previous.has_value()) {
        for (const std::string& registeredSymbol : previous->registeredSymbols) {
            backend_.UnregisterSymbolCallbacks(registeredSymbol);
        }
    }

    std::vector<std::string> registeredSymbols;
    if (!RegisterPluginSymbols(loadedModule.library, desc.entryPoint, registeredSymbols, result.errors)) {
        loadedModule.library.Reset();
        if (previous.has_value()) {
            RestoreOrClose(std::move(*previous), result.errors);
        }
        return result;
    }

    if (previous.has_value()) {
        previous->library.Reset();
    }

    result.loaded = true;
    result.loadedPath = loadedModule.loadedPath;
    result.registeredSymbols = registeredSymbols;
    plugins_.push_back(LoadedPlugin{
        .key = desc.key,
        .originalPath = loadedModule.originalPath,
        .loadedPath = loadedModule.loadedPath,
        .entryPoint = desc.entryPoint,
        .library = std::move(loadedModule.library),
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
        plugin->library.Reset();
    }
}

void NativeScriptPluginManager::Clear() noexcept {
    for (LoadedPlugin& plugin : plugins_) {
        for (const std::string& symbol : plugin.registeredSymbols) {
            backend_.UnregisterSymbol(symbol);
        }
        plugin.library.Reset();
    }
    plugins_.clear();
}

std::string NativeScriptPluginManager::NormalizeKey(std::string key, const std::filesystem::path& modulePath) {
    if (!key.empty()) {
        return key;
    }
    return std::filesystem::absolute(modulePath).string();
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
    kb::modules::EngineModuleLibrary& library,
    const std::string& entryPoint,
    std::vector<std::string>& registeredSymbols,
    std::vector<std::string>& errors) {
    void* symbol = library.FindSymbol(entryPoint, errors, "native script plugin");
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
    plugin.library.Reset();
}

} // namespace kb::script
