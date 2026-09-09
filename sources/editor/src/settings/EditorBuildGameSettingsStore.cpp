#include "settings/EditorBuildGameSettingsStore.hpp"

#include "engine/config/IniDocument.hpp"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <ShlObj.h>
#endif

#include <algorithm>
#include <array>
#include <cstdint>
#include <iomanip>
#include <optional>
#include <stdexcept>
#include <sstream>
#include <string_view>
#include <system_error>

namespace kb::editor {
namespace {

constexpr std::string_view kGlobal = "Packaging.Global";
constexpr std::string_view kWeb = "Packaging.Web";
constexpr std::string_view kLinux = "Packaging.Linux";

[[nodiscard]] std::optional<std::size_t> TargetIndex(kb::packaging::PackagingTarget target) noexcept {
    const auto targets = kb::packaging::PackagingTargets();
    for (std::size_t index = 0U; index < targets.size(); ++index) {
        if (targets[index].target == target) {
            return index;
        }
    }
    return std::nullopt;
}

[[nodiscard]] std::uint64_t PathHash(const std::filesystem::path& projectRoot) {
    const std::string text = std::filesystem::absolute(projectRoot).lexically_normal().generic_string();
    std::uint64_t hash = 1469598103934665603ULL;
    for (const unsigned char character : text) {
        hash ^= character;
        hash *= 1099511628211ULL;
    }
    return hash;
}

[[nodiscard]] std::filesystem::path LocalDataRoot() {
#if defined(_WIN32)
    PWSTR path = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_CREATE, nullptr, &path)) && path != nullptr) {
        std::filesystem::path result{ path };
        CoTaskMemFree(path);
        return result;
    }
#endif
    std::error_code error;
    return std::filesystem::temp_directory_path(error);
}

[[nodiscard]] std::string SectionFor(const kb::packaging::PackagingTargetSpec& target) {
    return "Packaging.Target." + std::string{ target.bakeId };
}

[[nodiscard]] std::filesystem::path ReadPath(
    const kb::config::IniDocument& document,
    std::string_view section,
    std::string_view key) {
    const std::optional<std::string_view> value = document.GetString(section, key);
    return value.has_value() ? std::filesystem::path{ *value } : std::filesystem::path{};
}

} // namespace

EditorBuildGameTargetSettings& EditorBuildGameSettings::For(kb::packaging::PackagingTarget target) {
    const std::optional<std::size_t> index = TargetIndex(target);
    if (!index.has_value()) throw std::out_of_range{ "Unknown packaging target." };
    return targets[*index];
}

const EditorBuildGameTargetSettings& EditorBuildGameSettings::For(kb::packaging::PackagingTarget target) const {
    const std::optional<std::size_t> index = TargetIndex(target);
    if (!index.has_value()) throw std::out_of_range{ "Unknown packaging target." };
    return targets[*index];
}

std::filesystem::path EditorBuildGameSettingsStore::FilePath(const std::filesystem::path& projectRoot) {
    std::ostringstream name;
    name << std::hex << std::setw(16) << std::setfill('0') << PathHash(projectRoot);
    return LocalDataRoot() / "21kb" / "Editor" / "Projects" / name.str() / "Packaging.ini";
}

EditorBuildGameSettingsLoadResult EditorBuildGameSettingsStore::Load(const std::filesystem::path& path) {
    EditorBuildGameSettingsLoadResult result;
    std::error_code filesystemError;
    if (!std::filesystem::exists(path, filesystemError) || filesystemError) {
        return result;
    }

    kb::config::IniDocument document;
    if (!document.Load(path, result.error)) {
        return result;
    }
    result.found = true;
    const std::filesystem::path builder = ReadPath(document, kGlobal, "BuilderExecutable");
    if (!builder.empty()) {
        result.settings.builderExecutable = builder;
    }
    result.settings.buildRoot = ReadPath(document, kGlobal, "BuildRoot");
    result.settings.emsdkRoot = ReadPath(document, kWeb, "EmsdkRoot");
    if (const auto value = document.GetString(kLinux, "Host")) result.settings.linuxHost = std::string{ *value };
    if (const auto value = document.GetString(kLinux, "User")) result.settings.linuxUser = std::string{ *value };
    if (const auto value = document.GetString(kLinux, "HostKey")) result.settings.linuxHostKey = std::string{ *value };
    result.settings.linuxPort = static_cast<std::uint16_t>(std::clamp<std::int64_t>(
        document.GetInt(kLinux, "Port").value_or(22), 1, 65535));
    if (const auto value = document.GetString(kLinux, "EngineRoot")) result.settings.linuxEngineRoot = std::string{ *value };
    if (const auto value = document.GetString(kLinux, "Display")) result.settings.linuxDisplay = std::string{ *value };
    result.settings.linuxIdentity = ReadPath(document, kLinux, "Identity");
    for (const kb::packaging::PackagingTargetSpec& target : kb::packaging::PackagingTargets()) {
        EditorBuildGameTargetSettings& targetSettings = result.settings.For(target.target);
        const std::string section = SectionFor(target);
        targetSettings.outputDirectory = ReadPath(document, section, "OutputDirectory");
        targetSettings.launchAfterBuild = document.GetBool(section, "LaunchAfterBuild").value_or(false);
        targetSettings.androidKeystore = ReadPath(document, section, "AndroidKeystore");
        if (const std::optional<std::string_view> alias = document.GetString(section, "AndroidKeyAlias")) {
            targetSettings.androidKeyAlias = std::string{ *alias };
        }
    }
    return result;
}

bool EditorBuildGameSettingsStore::Save(
    const std::filesystem::path& path,
    const EditorBuildGameSettings& settings,
    std::string& error) {
    error.clear();
    kb::config::IniDocument document;
    std::error_code filesystemError;
    const bool exists = std::filesystem::exists(path, filesystemError);
    if (filesystemError) {
        error = "Existing packaging settings could not be inspected: " + filesystemError.message();
        return false;
    }
    if (exists) {
        std::string loadError;
        if (!document.Load(path, loadError)) {
            error = "Existing packaging settings are invalid: " + loadError;
            return false;
        }
    }

    document.SetString(kGlobal, "BuilderExecutable", settings.builderExecutable.generic_string());
    document.SetString(kGlobal, "BuildRoot", settings.buildRoot.generic_string());
    document.SetString(kWeb, "EmsdkRoot", settings.emsdkRoot.generic_string());
    document.SetString(kLinux, "Host", settings.linuxHost);
    document.SetString(kLinux, "User", settings.linuxUser);
    document.SetString(kLinux, "HostKey", settings.linuxHostKey);
    document.SetInt(kLinux, "Port", settings.linuxPort);
    document.SetString(kLinux, "EngineRoot", settings.linuxEngineRoot);
    document.SetString(kLinux, "Display", settings.linuxDisplay);
    document.SetString(kLinux, "Identity", settings.linuxIdentity.generic_string());
    for (const kb::packaging::PackagingTargetSpec& target : kb::packaging::PackagingTargets()) {
        const EditorBuildGameTargetSettings& targetSettings = settings.For(target.target);
        const std::string section = SectionFor(target);
        document.SetString(section, "OutputDirectory", targetSettings.outputDirectory.generic_string());
        document.SetBool(section, "LaunchAfterBuild", targetSettings.launchAfterBuild);
        document.SetString(section, "AndroidKeystore", targetSettings.androidKeystore.generic_string());
        document.SetString(section, "AndroidKeyAlias", targetSettings.androidKeyAlias);
    }
    return document.Save(path, error);
}

} // namespace kb::editor
