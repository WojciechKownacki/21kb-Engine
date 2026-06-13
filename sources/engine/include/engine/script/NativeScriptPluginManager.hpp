#pragma once

#include "engine/modules/EngineModuleLoader.hpp"
#include "engine/script/NativeScriptBackend.hpp"

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace kb::script {

struct NativeScriptPluginLoadDesc {
    std::string key;
    std::filesystem::path modulePath;
    std::string entryPoint;
    bool shadowCopy = true;
    std::filesystem::path shadowCopyDirectory;
};

struct NativeScriptPluginLoadResult {
    bool loaded = false;
    std::filesystem::path loadedPath;
    std::vector<std::string> registeredSymbols;
    std::vector<std::string> errors;

    [[nodiscard]] bool Succeeded() const noexcept {
        return loaded && errors.empty();
    }
};

class NativeScriptPluginManager final {
public:
    explicit NativeScriptPluginManager(NativeScriptBackend& backend) noexcept;
    ~NativeScriptPluginManager();

    NativeScriptPluginManager(const NativeScriptPluginManager&) = delete;
    NativeScriptPluginManager& operator=(const NativeScriptPluginManager&) = delete;
    NativeScriptPluginManager(NativeScriptPluginManager&&) = delete;
    NativeScriptPluginManager& operator=(NativeScriptPluginManager&&) = delete;

    [[nodiscard]] NativeScriptPluginLoadResult LoadOrReload(NativeScriptPluginLoadDesc desc);
    [[nodiscard]] bool IsLoaded(std::string_view key) const noexcept;
    void Unload(std::string_view key) noexcept;
    void Clear() noexcept;

private:
    struct LoadedPlugin {
        std::string key;
        std::filesystem::path originalPath;
        std::filesystem::path loadedPath;
        std::string entryPoint;
        kb::modules::EngineModuleLibrary library;
        std::vector<std::string> registeredSymbols;
    };

    [[nodiscard]] static std::string NormalizeKey(std::string key, const std::filesystem::path& modulePath);
    [[nodiscard]] std::optional<LoadedPlugin> Detach(std::string_view key) noexcept;
    [[nodiscard]] bool RegisterPluginSymbols(kb::modules::EngineModuleLibrary& library, const std::string& entryPoint, std::vector<std::string>& registeredSymbols, std::vector<std::string>& errors);
    void RestoreOrClose(LoadedPlugin plugin, std::vector<std::string>& errors);

    NativeScriptBackend& backend_;
    kb::modules::EngineModuleLoader loader_;
    std::vector<LoadedPlugin> plugins_;
};

} // namespace kb::script
