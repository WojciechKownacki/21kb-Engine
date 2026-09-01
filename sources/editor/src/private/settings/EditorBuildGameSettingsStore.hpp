#pragma once

#include "engine/packaging/PackagingTargetCatalog.hpp"

#include <array>
#include <cstdint>
#include <filesystem>
#include <string>

namespace kb::editor {

struct EditorBuildGameTargetSettings {
    std::filesystem::path outputDirectory;
    bool launchAfterBuild = false;
    // Non-secret signing references. Passwords remain in memory only for the
    // active editor build and are never written to this store.
    std::filesystem::path androidKeystore;
    std::string androidKeyAlias;

    [[nodiscard]] bool operator==(const EditorBuildGameTargetSettings&) const noexcept = default;
};

struct EditorBuildGameSettings {
    std::filesystem::path builderExecutable = "python";
    std::filesystem::path buildRoot;
    std::filesystem::path emsdkRoot;
    std::string linuxHost;
    std::string linuxUser;
    std::string linuxHostKey;
    std::uint16_t linuxPort = 22U;
    std::string linuxEngineRoot;
    std::string linuxDisplay = ":0";
    std::filesystem::path linuxIdentity;
    std::array<EditorBuildGameTargetSettings, 6> targets{};

    [[nodiscard]] EditorBuildGameTargetSettings& For(kb::packaging::PackagingTarget target);
    [[nodiscard]] const EditorBuildGameTargetSettings& For(kb::packaging::PackagingTarget target) const;
    [[nodiscard]] bool operator==(const EditorBuildGameSettings&) const noexcept = default;
};

struct EditorBuildGameSettingsLoadResult {
    EditorBuildGameSettings settings{};
    bool found = false;
    std::string error;

    [[nodiscard]] bool Succeeded() const noexcept { return error.empty(); }
};

class EditorBuildGameSettingsStore final {
public:
    EditorBuildGameSettingsStore() = delete;

    [[nodiscard]] static std::filesystem::path FilePath(const std::filesystem::path& projectRoot);
    [[nodiscard]] static EditorBuildGameSettingsLoadResult Load(const std::filesystem::path& path);
    [[nodiscard]] static bool Save(
        const std::filesystem::path& path,
        const EditorBuildGameSettings& settings,
        std::string& error);
};

} // namespace kb::editor
