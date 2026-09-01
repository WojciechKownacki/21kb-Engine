#include "rendering/BuildGamePanelModel.hpp"
#include "packaging/EditorPackageInputValidation.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>

namespace kb::editor {
namespace {

constexpr std::array<BuildGameRowSpec, 3> kProjectRows{{
    { BuildGameField::ProjectName, "Project name", BuildGameRowKind::ReadOnly, true },
    { BuildGameField::Publisher, "Publisher", BuildGameRowKind::Text, true },
    { BuildGameField::Version, "Version", BuildGameRowKind::Text, true },
}};
constexpr std::array<BuildGameRowSpec, 3> kDesktopApplicationRows{{
    { BuildGameField::ProductName, "Product name", BuildGameRowKind::Text, true },
    { BuildGameField::ExecutableName, "Executable name", BuildGameRowKind::Text, true },
    { BuildGameField::ApplicationIcon, "Application icon", BuildGameRowKind::IconPicker, false },
}};
constexpr std::array<BuildGameRowSpec, 3> kWebApplicationRows{{
    { BuildGameField::ProductName, "Product name", BuildGameRowKind::Text, true },
    { BuildGameField::ExecutableName, "Bundle name", BuildGameRowKind::Text, true },
    { BuildGameField::ApplicationIcon, "Application icon", BuildGameRowKind::IconPicker, false },
}};
constexpr std::array<BuildGameRowSpec, 5> kAndroidApplicationRows{{
    { BuildGameField::ProductName, "Product name", BuildGameRowKind::Text, true },
    { BuildGameField::AndroidApplicationId, "Application id", BuildGameRowKind::Text, true },
    { BuildGameField::AndroidVersionCode, "Version code", BuildGameRowKind::Text, true },
    { BuildGameField::AndroidLabel, "Application label", BuildGameRowKind::Text, true },
    { BuildGameField::ApplicationIcon, "Application icon", BuildGameRowKind::IconPicker, false },
}};
constexpr std::array<BuildGameRowSpec, 1> kContentRows{{
    { BuildGameField::StartupMap, "Startup map", BuildGameRowKind::Text, true },
}};
constexpr std::array<BuildGameRowSpec, 4> kReleaseSigningRows{{
    { BuildGameField::AndroidKeystore, "Keystore", BuildGameRowKind::FilePicker, true },
    { BuildGameField::AndroidKeyAlias, "Key alias", BuildGameRowKind::Text, true },
    { BuildGameField::AndroidStorePassword, "Keystore password", BuildGameRowKind::Password, true },
    { BuildGameField::AndroidKeyPassword, "Key password", BuildGameRowKind::Password, true },
}};
constexpr std::array<BuildGameRowSpec, 4> kOutputRows{{
    { BuildGameField::OutputDirectory, "Output directory", BuildGameRowKind::FolderPicker, true },
    { BuildGameField::LaunchAfterBuild, "Launch after build", BuildGameRowKind::Checkbox, false },
    { BuildGameField::BuilderExecutable, "Builder executable", BuildGameRowKind::Text, true },
    { BuildGameField::BuildRoot, "Build directory", BuildGameRowKind::FolderPicker, true },
}};
constexpr std::array<BuildGameRowSpec, 1> kWebToolchainRows{{
    { BuildGameField::EmsdkRoot, "Emscripten SDK", BuildGameRowKind::FolderPicker, true },
}};
constexpr std::array<BuildGameRowSpec, 7> kLinuxToolchainRows{{
    { BuildGameField::LinuxHost, "SSH host", BuildGameRowKind::Text, true },
    { BuildGameField::LinuxUser, "SSH user", BuildGameRowKind::Text, true },
    { BuildGameField::LinuxHostKey, "SSH host key", BuildGameRowKind::Text, true },
    { BuildGameField::LinuxPort, "SSH port", BuildGameRowKind::Text, true },
    { BuildGameField::LinuxEngineRoot, "Remote engine root", BuildGameRowKind::Text, true },
    { BuildGameField::LinuxDisplay, "Display", BuildGameRowKind::Text, true },
    { BuildGameField::LinuxIdentity, "SSH identity", BuildGameRowKind::FilePicker, false },
}};

[[nodiscard]] const kb::packaging::PackagingTargetSpec* TargetSpec(kb::packaging::PackagingTarget target) noexcept {
    for (const kb::packaging::PackagingTargetSpec& candidate : kb::packaging::PackagingTargets()) {
        if (candidate.target == target) return &candidate;
    }
    return nullptr;
}

[[nodiscard]] std::string Trimmed(std::string_view value) {
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front())) != 0) value.remove_prefix(1U);
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back())) != 0) value.remove_suffix(1U);
    return std::string{ value };
}

[[nodiscard]] bool ValidVersion(std::string_view version) noexcept {
    if (version.empty() || version.size() > 64U) return false;
    for (const char character : version) {
        if (static_cast<unsigned char>(character) < 32U) return false;
    }
    return true;
}

[[nodiscard]] bool ValidWindowsVersion(std::string_view version) noexcept {
    const std::size_t suffix = version.find_first_of("-+");
    const std::string_view numeric = version.substr(0U, suffix);
    if (suffix != std::string_view::npos) {
        const std::string_view prerelease = version.substr(suffix + 1U);
        if (prerelease.empty()) return false;
        for (const char character : prerelease) {
            if (!package_input::IsAsciiAlphaNumeric(character) && character != '.' && character != '-') return false;
        }
    }
    if (numeric.empty() || numeric.back() == '.') return false;
    std::size_t begin = 0U;
    std::size_t components = 0U;
    while (begin < numeric.size()) {
        const std::size_t end = numeric.find('.', begin);
        const std::string_view component = numeric.substr(
            begin, end == std::string_view::npos ? numeric.size() - begin : end - begin);
        if (component.empty() || ++components > 4U) return false;
        std::uint32_t value = 0U;
        const auto parse = std::from_chars(component.data(), component.data() + component.size(), value);
        if (parse.ec != std::errc{} || parse.ptr != component.data() + component.size() || value > 65535U) return false;
        if (end == std::string_view::npos) break;
        begin = end + 1U;
    }
    return components > 0U;
}

[[nodiscard]] bool ValidText(std::string_view value, std::size_t maximum) noexcept {
    if (value.empty() || value.size() > maximum) return false;
    for (const char character : value) {
        if (static_cast<unsigned char>(character) < 32U) return false;
    }
    return true;
}

[[nodiscard]] bool ValidAndroidApplicationId(std::string_view value) noexcept {
    bool sawDot = false;
    bool segmentStart = true;
    for (const char character : value) {
        if (character == '.') {
            if (segmentStart) return false;
            sawDot = true;
            segmentStart = true;
            continue;
        }
        const unsigned char byte = static_cast<unsigned char>(character);
        if (segmentStart) {
            if (std::islower(byte) == 0) return false;
            segmentStart = false;
        } else if (std::islower(byte) == 0 && std::isdigit(byte) == 0 && character != '_') {
            return false;
        }
    }
    return sawDot && !segmentStart;
}

[[nodiscard]] std::string Missing(std::string_view label) {
    return std::string{ label } + " is required.";
}

} // namespace

std::vector<BuildGameSectionSpec> BuildGamePanelModel::Sections(kb::packaging::PackagingTarget target, bool release) {
    const kb::packaging::PackagingTargetSpec* targetSpec = TargetSpec(target);
    if (targetSpec == nullptr) return {};
    const std::span<const BuildGameRowSpec> application = targetSpec->needsAndroidMetadata
        ? std::span<const BuildGameRowSpec>{ kAndroidApplicationRows }
        : targetSpec->family == kb::packaging::PackagingTargetFamily::Web
            ? std::span<const BuildGameRowSpec>{ kWebApplicationRows }
            : std::span<const BuildGameRowSpec>{ kDesktopApplicationRows };
    std::vector<BuildGameSectionSpec> sections{
        { BuildGameSection::Project, "PROJECT", kProjectRows },
        { BuildGameSection::Application, targetSpec->needsAndroidMetadata ? "ANDROID APPLICATION" :
            targetSpec->family == kb::packaging::PackagingTargetFamily::Web ? "WEB APPLICATION" : "DESKTOP APPLICATION", application },
        { BuildGameSection::Content, "CONTENT", kContentRows },
    };
    if (targetSpec->needsAndroidMetadata && release)
        sections.push_back({ BuildGameSection::Signing, "SIGNING", kReleaseSigningRows });
    if (targetSpec->family == kb::packaging::PackagingTargetFamily::Web)
        sections.push_back({ BuildGameSection::Toolchain, "WEB TOOLCHAIN", kWebToolchainRows });
    if (targetSpec->target == kb::packaging::PackagingTarget::LinuxX64)
        sections.push_back({ BuildGameSection::Toolchain, "LINUX BUILD HOST", kLinuxToolchainRows });
    sections.push_back({ BuildGameSection::Output, "OUTPUT", kOutputRows });
    return sections;
}

std::string BuildGamePanelModel::Value(BuildGameField field, kb::packaging::PackagingTarget target,
    const kb::project::ProjectSettings& project, const EditorBuildGameSettings& local) {
    if (TargetSpec(target) == nullptr) return {};
    const EditorBuildGameTargetSettings& targetSettings = local.For(target);
    switch (field) {
    case BuildGameField::ProjectName: return project.name;
    case BuildGameField::Publisher: return project.publisher;
    case BuildGameField::Version: return project.version;
    case BuildGameField::ProductName: return project.gameName;
    case BuildGameField::ExecutableName: return project.executableName;
    case BuildGameField::ApplicationIcon: return project.applicationIcon.empty() ? "Optional - select PNG..." : project.applicationIcon;
    case BuildGameField::StartupMap: return project.defaultMap;
    case BuildGameField::AndroidApplicationId: return project.androidApplicationId;
    case BuildGameField::AndroidVersionCode: return std::to_string(project.androidVersionCode);
    case BuildGameField::AndroidLabel: return project.androidLabel;
    case BuildGameField::AndroidKeystore: return targetSettings.androidKeystore.empty() ? "Development only" : targetSettings.androidKeystore.generic_string();
    case BuildGameField::AndroidKeyAlias: return targetSettings.androidKeyAlias.empty() ? "Development only" : targetSettings.androidKeyAlias;
    case BuildGameField::AndroidStorePassword:
    case BuildGameField::AndroidKeyPassword: return "Entered for one build only";
    case BuildGameField::OutputDirectory: return targetSettings.outputDirectory.empty() ? "Select folder..." : targetSettings.outputDirectory.generic_string();
    case BuildGameField::LaunchAfterBuild: return targetSettings.launchAfterBuild ? "true" : "false";
    case BuildGameField::BuilderExecutable: return local.builderExecutable.generic_string();
    case BuildGameField::BuildRoot: return local.buildRoot.empty() ? "Select build directory..." : local.buildRoot.generic_string();
    case BuildGameField::EmsdkRoot: return local.emsdkRoot.empty() ? "Select emsdk..." : local.emsdkRoot.generic_string();
    case BuildGameField::LinuxHost: return local.linuxHost;
    case BuildGameField::LinuxUser: return local.linuxUser;
    case BuildGameField::LinuxHostKey: return local.linuxHostKey;
    case BuildGameField::LinuxPort: return std::to_string(local.linuxPort);
    case BuildGameField::LinuxEngineRoot: return local.linuxEngineRoot;
    case BuildGameField::LinuxDisplay: return local.linuxDisplay;
    case BuildGameField::LinuxIdentity: return local.linuxIdentity.empty() ? "Optional - select private key..." : local.linuxIdentity.generic_string();
    case BuildGameField::None: break;
    }
    return {};
}

BuildGameValidation BuildGamePanelModel::Validate(kb::packaging::PackagingTarget target,
    const kb::project::ProjectSettings& project, const EditorBuildGameSettings& local,
    bool release, bool jobRunning, bool hasStorePassword, bool hasKeyPassword) {
    if (jobRunning) return { false, "A package job is running." };
    if (Trimmed(project.name).empty()) return { false, Missing("Project name") };
    if (!ValidText(Trimmed(project.gameName), 128U)) return { false, "Product name must contain 1-128 printable characters." };
    if (!ValidText(Trimmed(project.publisher), 128U)) return { false, "Publisher must contain 1-128 printable characters." };
    const std::string version = Trimmed(project.version);
    if (!ValidVersion(version)) return { false, "Version must contain 1-64 printable characters." };
    if (Trimmed(project.defaultMap).empty()) return { false, Missing("Startup map") };
    const kb::packaging::PackagingTargetSpec* spec = TargetSpec(target);
    if (spec == nullptr) return { false, "The selected package target is invalid." };
    if (spec->target == kb::packaging::PackagingTarget::WindowsX64 && !ValidWindowsVersion(version))
        return { false, "Windows version must contain 1-4 numeric components up to 65535 and an optional prerelease suffix." };
    const std::string packageName = Trimmed(
        spec->needsExecutableName ? project.executableName : project.gameName);
    if (!package_input::IsValidPackageName(
            packageName, spec->target == kb::packaging::PackagingTarget::WindowsX64))
        return { false, "Executable or package name must use 1-80 ASCII letters, digits, spaces, dots, dashes, or underscores." };
    if (spec->needsAndroidMetadata) {
        if (!ValidAndroidApplicationId(Trimmed(project.androidApplicationId)))
            return { false, "Android application id must be a lowercase dotted identifier." };
        if (project.androidVersionCode == 0U || project.androidVersionCode > 2'100'000'000U)
            return { false, "Android version code must be between 1 and 2100000000." };
        if (!ValidText(Trimmed(project.androidLabel), 128U))
            return { false, "Android application label must contain 1-128 printable characters." };
    }
    const EditorBuildGameTargetSettings& targetSettings = local.For(target);
    if (release && spec->needsAndroidMetadata) {
        if (targetSettings.androidKeystore.empty()) return { false, Missing("Android keystore") };
        if (!package_input::IsValidAndroidKeyAlias(Trimmed(targetSettings.androidKeyAlias)))
            return { false, "Android key alias must use 1-128 ASCII letters, digits, dots, dashes, or underscores." };
        if (!hasStorePassword) return { false, Missing("Android keystore password") };
        if (!hasKeyPassword) return { false, Missing("Android key password") };
    }
    if (targetSettings.outputDirectory.empty()) return { false, Missing("Output directory") };
    if (local.builderExecutable.empty()) return { false, Missing("Builder executable") };
    if (local.buildRoot.empty()) return { false, Missing("Build directory") };
    if (spec->family == kb::packaging::PackagingTargetFamily::Web && local.emsdkRoot.empty())
        return { false, Missing("Emscripten SDK") };
    if (spec->target == kb::packaging::PackagingTarget::LinuxX64) {
        if (!package_input::IsValidLinuxHost(Trimmed(local.linuxHost))) return { false, "Linux SSH host is invalid." };
        if (!package_input::IsValidLinuxUser(Trimmed(local.linuxUser))) return { false, "Linux SSH user is invalid." };
        if (!package_input::IsValidLinuxHostKey(Trimmed(local.linuxHostKey)))
            return { false, "Linux host key must contain an allowlisted type and base64 public key." };
        if (local.linuxPort == 0U) return { false, "Linux SSH port is invalid." };
        if (!package_input::IsValidLinuxEngineRoot(Trimmed(local.linuxEngineRoot)))
            return { false, "Linux engine root must be a safe absolute POSIX path." };
        if (!package_input::IsValidLinuxDisplay(Trimmed(local.linuxDisplay))) return { false, "Linux X11 display is invalid." };
    }
    return { true, "Ready to build " + std::string{ spec->displayName } + '.' };
}

bool BuildGamePanelModel::ApplyText(BuildGameField field, std::string_view value,
    kb::packaging::PackagingTarget target, kb::project::ProjectSettings& project,
    EditorBuildGameSettings& local, std::string& error) {
    error.clear();
    const std::string text = Trimmed(value);
    switch (field) {
    case BuildGameField::Publisher: project.publisher = text; return true;
    case BuildGameField::Version:
        if (!ValidVersion(text)) { error = "Version must contain 1-64 printable characters."; return false; }
        project.version = text; return true;
    case BuildGameField::ProductName: project.gameName = text; return true;
    case BuildGameField::ExecutableName:
        if (!package_input::IsValidPackageName(
                text, target == kb::packaging::PackagingTarget::WindowsX64)) {
            error = "Executable name is not valid for the selected target.";
            return false;
        }
        project.executableName = text; return true;
    case BuildGameField::StartupMap: project.defaultMap = text; return true;
    case BuildGameField::AndroidApplicationId:
        if (!ValidAndroidApplicationId(text)) { error = "Android application id must be a lowercase dotted identifier."; return false; }
        project.androidApplicationId = text; return true;
    case BuildGameField::AndroidVersionCode: {
        std::uint32_t code = 0U;
        const auto parsed = std::from_chars(text.data(), text.data() + text.size(), code);
        if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size() || code == 0U || code > 2'100'000'000U) {
            error = "Android version code must be between 1 and 2100000000.";
            return false;
        }
        project.androidVersionCode = code;
        return true;
    }
    case BuildGameField::AndroidLabel: project.androidLabel = text; return true;
    case BuildGameField::AndroidKeyAlias: local.For(target).androidKeyAlias = text; return true;
    case BuildGameField::BuilderExecutable: local.builderExecutable = text; return true;
    case BuildGameField::BuildRoot: local.buildRoot = text; return true;
    case BuildGameField::EmsdkRoot: local.emsdkRoot = text; return true;
    case BuildGameField::LinuxHost: local.linuxHost = text; return true;
    case BuildGameField::LinuxUser: local.linuxUser = text; return true;
    case BuildGameField::LinuxHostKey: local.linuxHostKey = text; return true;
    case BuildGameField::LinuxPort: {
        std::uint32_t port = 0U;
        const auto parsed = std::from_chars(text.data(), text.data() + text.size(), port);
        if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size() || port == 0U || port > 65535U) {
            error = "Linux SSH port must be between 1 and 65535.";
            return false;
        }
        local.linuxPort = static_cast<std::uint16_t>(port);
        return true;
    }
    case BuildGameField::LinuxEngineRoot: local.linuxEngineRoot = text; return true;
    case BuildGameField::LinuxDisplay: local.linuxDisplay = text; return true;
    case BuildGameField::OutputDirectory: local.For(target).outputDirectory = text; return true;
    case BuildGameField::None:
    case BuildGameField::ProjectName:
    case BuildGameField::ApplicationIcon:
    case BuildGameField::AndroidKeystore:
    case BuildGameField::AndroidStorePassword:
    case BuildGameField::AndroidKeyPassword:
    case BuildGameField::LinuxIdentity:
    case BuildGameField::LaunchAfterBuild:
        error = "The selected field is not text-editable.";
        return false;
    }
    return false;
}

bool BuildGamePanelModel::InsertPrintableText(std::string& buffer, bool& selectAll,
    std::string_view text, std::size_t maximumLength) {
    std::string printable;
    printable.reserve(std::min(text.size(), maximumLength));
    for (const char character : text) {
        const unsigned char byte = static_cast<unsigned char>(character);
        if (byte >= 32U && byte <= 126U) printable.push_back(character);
    }
    if (printable.empty()) return false;
    if (selectAll) {
        buffer.clear();
        selectAll = false;
    }
    if (buffer.size() >= maximumLength) return false;
    printable.resize(std::min(printable.size(), maximumLength - buffer.size()));
    buffer.append(printable);
    return true;
}

std::vector<BuildGameField> BuildGamePanelModel::TextFocusOrder(kb::packaging::PackagingTarget target, bool release) {
    std::vector<BuildGameField> fields;
    for (const BuildGameSectionSpec& section : Sections(target, release)) {
        for (const BuildGameRowSpec& row : section.rows)
            if (row.kind == BuildGameRowKind::Text || row.kind == BuildGameRowKind::Password) fields.push_back(row.field);
    }
    return fields;
}

} // namespace kb::editor
