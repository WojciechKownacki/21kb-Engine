#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace kb::modules {

class EngineModuleLibrary final {
public:
    EngineModuleLibrary() noexcept = default;
    ~EngineModuleLibrary();

    EngineModuleLibrary(const EngineModuleLibrary&) = delete;
    EngineModuleLibrary& operator=(const EngineModuleLibrary&) = delete;
    EngineModuleLibrary(EngineModuleLibrary&& other) noexcept;
    EngineModuleLibrary& operator=(EngineModuleLibrary&& other) noexcept;

    [[nodiscard]] bool IsLoaded() const noexcept { return handle_ != nullptr; }
    [[nodiscard]] const std::filesystem::path& OriginalPath() const noexcept { return originalPath_; }
    [[nodiscard]] const std::filesystem::path& LoadedPath() const noexcept { return loadedPath_; }
    [[nodiscard]] void* Handle() const noexcept { return handle_; }

    [[nodiscard]] void* FindSymbol(std::string_view name, std::vector<std::string>& errors, std::string_view diagnosticLabel = "engine module") const;
    void Reset() noexcept;

private:
    friend class EngineModuleLoader;

    EngineModuleLibrary(std::filesystem::path originalPath, std::filesystem::path loadedPath, void* handle) noexcept;

    std::filesystem::path originalPath_;
    std::filesystem::path loadedPath_;
    void* handle_ = nullptr;
};

struct EngineModuleLoadDesc {
    std::string key;
    std::filesystem::path modulePath;
    bool shadowCopy = true;
    std::filesystem::path shadowCopyDirectory;
    std::string shadowCopyDirectoryName = "21kb_engine_modules";
    std::string diagnosticLabel = "engine module";
};

struct EngineModuleLoadResult {
    bool loaded = false;
    std::filesystem::path originalPath;
    std::filesystem::path loadedPath;
    std::uint64_t reloadSerial = 0U;
    EngineModuleLibrary library;
    std::vector<std::string> errors;

    [[nodiscard]] bool Succeeded() const noexcept {
        return loaded && library.IsLoaded() && errors.empty();
    }
};

class EngineModuleLoader final {
public:
    EngineModuleLoader() = default;

    [[nodiscard]] EngineModuleLoadResult Load(EngineModuleLoadDesc desc);
    [[nodiscard]] std::uint64_t NextReloadSerial() const noexcept { return reloadSerial_; }

private:
    [[nodiscard]] static std::string NormalizeKey(std::string key, const std::filesystem::path& modulePath);
    [[nodiscard]] std::filesystem::path ResolveShadowCopyPath(
        std::string_view key,
        const std::filesystem::path& modulePath,
        const std::filesystem::path& shadowCopyDirectory,
        std::string_view shadowCopyDirectoryName,
        std::uint64_t& serial);
    [[nodiscard]] static bool CopyForLoad(
        const std::filesystem::path& source,
        const std::filesystem::path& destination,
        std::string_view diagnosticLabel,
        std::vector<std::string>& errors);
    [[nodiscard]] static void* LoadNativeLibrary(
        const std::filesystem::path& path,
        std::string_view diagnosticLabel,
        std::vector<std::string>& errors);

    std::uint64_t reloadSerial_ = 1U;
};

} // namespace kb::modules
