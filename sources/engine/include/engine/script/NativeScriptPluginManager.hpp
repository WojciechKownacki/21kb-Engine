#pragma once

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
        void* library = nullptr;
        std::vector<std::string> registeredSymbols;
    };

    [[nodiscard]] static std::string NormalizeKey(std::string key, const std::filesystem::path& modulePath);
    [[nodiscard]] std::filesystem::path ResolveShadowCopyPath(
        std::string_view key,
        const std::filesystem::path& modulePath,
        const std::filesystem::path& shadowCopyDirectory);
    [[nodiscard]] static bool CopyForLoad(
        const std::filesystem::path& source,
        const std::filesystem::path& destination,
        std::vector<std::string>& errors);
    static void* LoadNativeLibrary(const std::filesystem::path& path, std::vector<std::string>& errors);
    static void* FindNativeSymbol(void* library, const std::string& name, std::vector<std::string>& errors);
    static void CloseNativeLibrary(void* library) noexcept;
    [[nodiscard]] std::optional<LoadedPlugin> Detach(std::string_view key) noexcept;
    [[nodiscard]] bool RegisterPluginSymbols(void* library, const std::string& entryPoint, std::vector<std::string>& registeredSymbols, std::vector<std::string>& errors);
    void RestoreOrClose(LoadedPlugin plugin, std::vector<std::string>& errors);

    NativeScriptBackend& backend_;
    std::vector<LoadedPlugin> plugins_;
    std::uint64_t reloadSerial_ = 1U;
};

} // namespace kb::script
