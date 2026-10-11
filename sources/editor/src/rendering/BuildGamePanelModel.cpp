#include "rendering/BuildGamePanelModel.hpp"
#include "localization/EditorLocalization.hpp"
#include "packaging/EditorPackageInputValidation.hpp"
#include "engine/platform/CrashReporting.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>

namespace kb::editor {
namespace {

constexpr std::array<BuildGameRowSpec, 3> kProjectRows{{
    { BuildGameField::ProjectName, [] { return EditorText("buildGame.field.projectName", "Project name"); }, BuildGameRowKind::ReadOnly, true },
    { BuildGameField::Publisher, [] { return EditorText("buildGame.field.publisher", "Publisher"); }, BuildGameRowKind::Text, true },
    { BuildGameField::Version, [] { return EditorText("buildGame.field.version", "Version"); }, BuildGameRowKind::Text, true },
}};
constexpr std::array<BuildGameRowSpec, 3> kDesktopApplicationRows{{
    { BuildGameField::ProductName, [] { return EditorText("buildGame.field.productName", "Product name"); }, BuildGameRowKind::Text, true },
    { BuildGameField::ExecutableName, [] { return EditorText("buildGame.field.executableName", "Executable name"); }, BuildGameRowKind::Text, true },
    { BuildGameField::ApplicationIcon, [] { return EditorText("buildGame.field.applicationIcon", "Application icon"); }, BuildGameRowKind::IconPicker, false },
}};
// The Windows player is the one host with crash reports, so only it offers an endpoint.
constexpr std::array<BuildGameRowSpec, 4> kWindowsApplicationRows{{
    { BuildGameField::ProductName, [] { return EditorText("buildGame.field.productName", "Product name"); }, BuildGameRowKind::Text, true },
    { BuildGameField::ExecutableName, [] { return EditorText("buildGame.field.executableName", "Executable name"); }, BuildGameRowKind::Text, true },
    { BuildGameField::ApplicationIcon, [] { return EditorText("buildGame.field.applicationIcon", "Application icon"); }, BuildGameRowKind::IconPicker, false },
    { BuildGameField::CrashReportUrl, [] { return EditorText("buildGame.field.crashReportURL", "Crash report URL"); }, BuildGameRowKind::Text, false },
}};
constexpr std::array<BuildGameRowSpec, 3> kWebApplicationRows{{
    { BuildGameField::ProductName, [] { return EditorText("buildGame.field.productName", "Product name"); }, BuildGameRowKind::Text, true },
    { BuildGameField::ExecutableName, [] { return EditorText("buildGame.field.bundleName", "Bundle name"); }, BuildGameRowKind::Text, true },
    { BuildGameField::ApplicationIcon, [] { return EditorText("buildGame.field.applicationIcon", "Application icon"); }, BuildGameRowKind::IconPicker, false },
}};
constexpr std::array<BuildGameRowSpec, 5> kAndroidApplicationRows{{
    { BuildGameField::ProductName, [] { return EditorText("buildGame.field.productName", "Product name"); }, BuildGameRowKind::Text, true },
    { BuildGameField::AndroidApplicationId, [] { return EditorText("buildGame.field.applicationId", "Application id"); }, BuildGameRowKind::Text, true },
    { BuildGameField::AndroidVersionCode, [] { return EditorText("buildGame.field.versionCode", "Version code"); }, BuildGameRowKind::Text, true },
    { BuildGameField::AndroidLabel, [] { return EditorText("buildGame.field.applicationLabel", "Application label"); }, BuildGameRowKind::Text, true },
    { BuildGameField::ApplicationIcon, [] { return EditorText("buildGame.field.applicationIcon", "Application icon"); }, BuildGameRowKind::IconPicker, false },
}};
constexpr std::array<BuildGameRowSpec, 1> kContentRows{{
    { BuildGameField::StartupMap, [] { return EditorText("buildGame.field.startupMap", "Startup map"); }, BuildGameRowKind::Text, true },
}};
constexpr std::array<BuildGameRowSpec, 4> kReleaseSigningRows{{
    { BuildGameField::AndroidKeystore, [] { return EditorText("buildGame.field.keystore", "Keystore"); }, BuildGameRowKind::FilePicker, true },
    { BuildGameField::AndroidKeyAlias, [] { return EditorText("buildGame.field.keyAlias", "Key alias"); }, BuildGameRowKind::Text, true },
    { BuildGameField::AndroidStorePassword, [] { return EditorText("buildGame.field.keystorePassword", "Keystore password"); }, BuildGameRowKind::Password, true },
    { BuildGameField::AndroidKeyPassword, [] { return EditorText("buildGame.field.keyPassword", "Key password"); }, BuildGameRowKind::Password, true },
}};
// Authenticode is optional: an unsigned Release still builds. With a certificate,
// every shipped image must carry a verified signature or the package fails.
constexpr std::array<BuildGameRowSpec, 4> kWindowsSigningRows{{
    { BuildGameField::WindowsCertificateThumbprint, [] { return EditorText("buildGame.field.certificateThumbprint", "Certificate thumbprint"); }, BuildGameRowKind::Text, false },
    { BuildGameField::WindowsCertificateFile, [] { return EditorText("buildGame.field.certificateFile", "Certificate file"); }, BuildGameRowKind::FilePicker, false },
    { BuildGameField::WindowsCertificatePassword, [] { return EditorText("buildGame.field.certificatePassword", "Certificate password"); }, BuildGameRowKind::Password, false },
    { BuildGameField::WindowsTimestampUrl, [] { return EditorText("buildGame.field.timestampURL", "Timestamp URL"); }, BuildGameRowKind::Text, false },
}};
constexpr std::array<BuildGameRowSpec, 4> kOutputRows{{
    { BuildGameField::OutputDirectory, [] { return EditorText("buildGame.field.outputDirectory", "Output directory"); }, BuildGameRowKind::FolderPicker, true },
    { BuildGameField::LaunchAfterBuild, [] { return EditorText("buildGame.field.launchAfterBuild", "Launch after build"); }, BuildGameRowKind::Checkbox, false },
    { BuildGameField::BuilderExecutable, [] { return EditorText("buildGame.field.builderExecutable", "Builder executable"); }, BuildGameRowKind::Text, true },
    { BuildGameField::BuildRoot, [] { return EditorText("buildGame.field.buildDirectory", "Build directory"); }, BuildGameRowKind::FolderPicker, true },
}};
constexpr std::array<BuildGameRowSpec, 1> kWebToolchainRows{{
    { BuildGameField::EmsdkRoot, [] { return EditorText("buildGame.field.emscriptenSDK", "Emscripten SDK"); }, BuildGameRowKind::FolderPicker, true },
}};
constexpr std::array<BuildGameRowSpec, 7> kLinuxToolchainRows{{
    { BuildGameField::LinuxHost, [] { return EditorText("buildGame.field.sshHost", "SSH host"); }, BuildGameRowKind::Text, true },
    { BuildGameField::LinuxUser, [] { return EditorText("buildGame.field.sshUser", "SSH user"); }, BuildGameRowKind::Text, true },
    { BuildGameField::LinuxHostKey, [] { return EditorText("buildGame.field.sshHostKey", "SSH host key"); }, BuildGameRowKind::Text, true },
    { BuildGameField::LinuxPort, [] { return EditorText("buildGame.field.sshPort", "SSH port"); }, BuildGameRowKind::Text, true },
    { BuildGameField::LinuxEngineRoot, [] { return EditorText("buildGame.field.remoteEngineRoot", "Remote engine root"); }, BuildGameRowKind::Text, true },
    { BuildGameField::LinuxDisplay, [] { return EditorText("buildGame.field.display", "Display"); }, BuildGameRowKind::Text, true },
    { BuildGameField::LinuxIdentity, [] { return EditorText("buildGame.field.sshIdentity", "SSH identity"); }, BuildGameRowKind::FilePicker, false },
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
    return EditorLocalization::Format("buildGame.required", "{field} is required.", { { "field", std::string{ label } } });
}

} // namespace

std::string_view BuildGamePanelModel::TargetDisplayName(kb::packaging::PackagingTarget target) {
    switch (target) {
    case kb::packaging::PackagingTarget::WindowsX64: return EditorText("buildGame.target.windowsX64", "Windows 64 bit");
    case kb::packaging::PackagingTarget::AndroidAstcArm64: return EditorText("buildGame.target.androidAstc", "Android ASTC");
    case kb::packaging::PackagingTarget::AndroidEtc2Arm64: return EditorText("buildGame.target.androidEtc2", "Android ETC2");
    case kb::packaging::PackagingTarget::LinuxX64: return EditorText("buildGame.target.linuxX64", "Linux 64 bit");
    case kb::packaging::PackagingTarget::WebGlWasm32: return EditorText("buildGame.target.webGl", "WebGL");
    case kb::packaging::PackagingTarget::WebGpuWasm32: return EditorText("buildGame.target.webGpu", "WebGPU");
    }
    const kb::packaging::PackagingTargetSpec* spec = TargetSpec(target);
    return spec != nullptr ? spec->displayName : std::string_view{};
}

std::string_view BuildGamePanelModel::TargetSummary(kb::packaging::PackagingTarget target) {
    switch (target) {
    case kb::packaging::PackagingTarget::WindowsX64:
    case kb::packaging::PackagingTarget::LinuxX64:
        return EditorText("buildGame.targetSummary.desktop", "Player package | x86-64 | Portable folder");
    case kb::packaging::PackagingTarget::AndroidAstcArm64:
        return EditorText("buildGame.targetSummary.androidAstc", "Player package | arm64-v8a | APK | ASTC");
    case kb::packaging::PackagingTarget::AndroidEtc2Arm64:
        return EditorText("buildGame.targetSummary.androidEtc2", "Player package | arm64-v8a | APK | ETC2");
    case kb::packaging::PackagingTarget::WebGlWasm32:
        return EditorText("buildGame.targetSummary.webGl", "Browser package | wasm32 | WebGL");
    case kb::packaging::PackagingTarget::WebGpuWasm32:
        return EditorText("buildGame.targetSummary.webGpu", "Browser package | wasm32 | WebGPU");
    }
    const kb::packaging::PackagingTargetSpec* spec = TargetSpec(target);
    return spec != nullptr ? spec->summary : std::string_view{};
}

std::vector<BuildGameSectionSpec> BuildGamePanelModel::Sections(kb::packaging::PackagingTarget target, bool release) {
    const kb::packaging::PackagingTargetSpec* targetSpec = TargetSpec(target);
    if (targetSpec == nullptr) return {};
    const std::span<const BuildGameRowSpec> application = targetSpec->needsAndroidMetadata
        ? std::span<const BuildGameRowSpec>{ kAndroidApplicationRows }
        : targetSpec->family == kb::packaging::PackagingTargetFamily::Web
            ? std::span<const BuildGameRowSpec>{ kWebApplicationRows }
            : target == kb::packaging::PackagingTarget::WindowsX64
                ? std::span<const BuildGameRowSpec>{ kWindowsApplicationRows }
                : std::span<const BuildGameRowSpec>{ kDesktopApplicationRows };
    std::vector<BuildGameSectionSpec> sections{
        { BuildGameSection::Project, EditorText("buildGame.section.project", "PROJECT"), kProjectRows },
        { BuildGameSection::Application, targetSpec->needsAndroidMetadata ? EditorText("buildGame.section.androidApplication", "ANDROID APPLICATION") :
            targetSpec->family == kb::packaging::PackagingTargetFamily::Web ? EditorText("buildGame.section.webApplication", "WEB APPLICATION") : EditorText("buildGame.section.desktopApplication", "DESKTOP APPLICATION"), application },
        { BuildGameSection::Content, EditorText("buildGame.section.content", "CONTENT"), kContentRows },
    };
    if (targetSpec->needsAndroidMetadata && release)
        sections.push_back({ BuildGameSection::Signing, EditorText("buildGame.section.signing", "SIGNING"), kReleaseSigningRows });
    if (target == kb::packaging::PackagingTarget::WindowsX64 && release)
        sections.push_back({ BuildGameSection::Signing, EditorText("buildGame.section.signing", "SIGNING"), kWindowsSigningRows });
    if (targetSpec->family == kb::packaging::PackagingTargetFamily::Web)
        sections.push_back({ BuildGameSection::Toolchain, EditorText("buildGame.section.webToolchain", "WEB TOOLCHAIN"), kWebToolchainRows });
    if (targetSpec->target == kb::packaging::PackagingTarget::LinuxX64)
        sections.push_back({ BuildGameSection::Toolchain, EditorText("buildGame.section.linuxBuildHost", "LINUX BUILD HOST"), kLinuxToolchainRows });
    sections.push_back({ BuildGameSection::Output, EditorText("buildGame.section.output", "OUTPUT"), kOutputRows });
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
    case BuildGameField::ApplicationIcon: return project.applicationIcon.empty() ? std::string{ EditorText("buildGame.value.selectPng", "Optional - select PNG...") } : project.applicationIcon;
    case BuildGameField::StartupMap: return project.defaultMap;
    case BuildGameField::AndroidApplicationId: return project.androidApplicationId;
    case BuildGameField::AndroidVersionCode: return std::to_string(project.androidVersionCode);
    case BuildGameField::AndroidLabel: return project.androidLabel;
    case BuildGameField::AndroidKeystore: return targetSettings.androidKeystore.empty() ? std::string{ EditorText("buildGame.value.developmentOnly", "Development only") } : targetSettings.androidKeystore.generic_string();
    case BuildGameField::AndroidKeyAlias: return targetSettings.androidKeyAlias.empty() ? std::string{ EditorText("buildGame.value.developmentOnly", "Development only") } : targetSettings.androidKeyAlias;
    case BuildGameField::AndroidStorePassword:
    case BuildGameField::AndroidKeyPassword: return std::string{ EditorText("buildGame.value.oneBuildOnly", "Entered for one build only") };
    case BuildGameField::OutputDirectory: return targetSettings.outputDirectory.empty() ? std::string{ EditorText("buildGame.value.selectFolder", "Select folder...") } : targetSettings.outputDirectory.generic_string();
    case BuildGameField::LaunchAfterBuild: return targetSettings.launchAfterBuild ? "true" : "false";
    case BuildGameField::BuilderExecutable: return local.builderExecutable.generic_string();
    case BuildGameField::BuildRoot: return local.buildRoot.empty() ? std::string{ EditorText("buildGame.value.selectBuildDirectory", "Select build directory...") } : local.buildRoot.generic_string();
    case BuildGameField::EmsdkRoot: return local.emsdkRoot.empty() ? std::string{ EditorText("buildGame.value.selectEmsdk", "Select emsdk...") } : local.emsdkRoot.generic_string();
    case BuildGameField::LinuxHost: return local.linuxHost;
    case BuildGameField::LinuxUser: return local.linuxUser;
    case BuildGameField::LinuxHostKey: return local.linuxHostKey;
    case BuildGameField::LinuxPort: return std::to_string(local.linuxPort);
    case BuildGameField::LinuxEngineRoot: return local.linuxEngineRoot;
    case BuildGameField::LinuxDisplay: return local.linuxDisplay;
    case BuildGameField::LinuxIdentity: return local.linuxIdentity.empty() ? std::string{ EditorText("buildGame.value.selectPrivateKey", "Optional - select private key...") } : local.linuxIdentity.generic_string();
    case BuildGameField::CrashReportUrl:
        return project.crashReportUploadUrl.empty() ? std::string{ EditorText("buildGame.value.crashReportsLocal", "Optional - players' crash reports stay local") } : project.crashReportUploadUrl;
    case BuildGameField::WindowsCertificateThumbprint:
        return targetSettings.windowsCertificateThumbprint.empty() ? std::string{ EditorText("buildGame.value.thumbprintHint", "Optional - SHA-1 of a certificate in your store") } : targetSettings.windowsCertificateThumbprint;
    case BuildGameField::WindowsCertificateFile:
        return targetSettings.windowsCertificateFile.empty() ? std::string{ EditorText("buildGame.value.selectPfx", "Optional - select PFX...") } : targetSettings.windowsCertificateFile.generic_string();
    case BuildGameField::WindowsCertificatePassword: return std::string{ EditorText("buildGame.value.oneBuildOnly", "Entered for one build only") };
    case BuildGameField::WindowsTimestampUrl:
        return targetSettings.windowsTimestampUrl.empty() ? std::string{ EditorText("buildGame.value.timestampHint", "Optional - RFC 3161 timestamp service") } : targetSettings.windowsTimestampUrl;
    case BuildGameField::None: break;
    }
    return {};
}

BuildGameValidation BuildGamePanelModel::Validate(kb::packaging::PackagingTarget target,
    const kb::project::ProjectSettings& project, const EditorBuildGameSettings& local,
    bool release, bool jobRunning, bool hasStorePassword, bool hasKeyPassword, bool hasCertificatePassword) {
    if (jobRunning) return { false, std::string{ EditorText("buildGame.invalid.jobRunning", "A package job is running.") } };
    if (Trimmed(project.name).empty()) return { false, Missing(EditorText("buildGame.field.projectName", "Project name")) };
    if (!ValidText(Trimmed(project.gameName), 128U)) {
        return { false, std::string{ EditorText("buildGame.invalid.productName", "Product name must contain 1-128 printable characters.") } };
    }
    if (!ValidText(Trimmed(project.publisher), 128U)) {
        return { false, std::string{ EditorText("buildGame.invalid.publisher", "Publisher must contain 1-128 printable characters.") } };
    }
    const std::string version = Trimmed(project.version);
    if (!ValidVersion(version)) {
        return { false, std::string{ EditorText("buildGame.invalid.version", "Version must contain 1-64 printable characters.") } };
    }
    if (Trimmed(project.defaultMap).empty()) return { false, Missing(EditorText("buildGame.field.startupMap", "Startup map")) };
    const kb::packaging::PackagingTargetSpec* spec = TargetSpec(target);
    if (spec == nullptr) return { false, std::string{ EditorText("buildGame.invalid.target", "The selected package target is invalid.") } };
    if (spec->target == kb::packaging::PackagingTarget::WindowsX64 && !ValidWindowsVersion(version))
        return { false, std::string{ EditorText("buildGame.invalid.windowsVersion",
            "Windows version must contain 1-4 numeric components up to 65535 and an optional prerelease suffix.") } };
    const std::string packageName = Trimmed(
        spec->needsExecutableName ? project.executableName : project.gameName);
    if (!package_input::IsValidPackageName(
            packageName, spec->target == kb::packaging::PackagingTarget::WindowsX64))
        return { false, std::string{ EditorText("buildGame.invalid.executableName",
            "Executable or package name must use 1-80 ASCII letters, digits, spaces, dots, dashes, or underscores.") } };
    if (spec->needsAndroidMetadata) {
        if (!ValidAndroidApplicationId(Trimmed(project.androidApplicationId)))
            return { false, std::string{ EditorText("buildGame.invalid.androidApplicationId", "Android application id must be a lowercase dotted identifier.") } };
        if (project.androidVersionCode == 0U || project.androidVersionCode > 2'100'000'000U)
            return { false, std::string{ EditorText("buildGame.invalid.androidVersionCode", "Android version code must be between 1 and 2100000000.") } };
        if (!ValidText(Trimmed(project.androidLabel), 128U))
            return { false, std::string{ EditorText("buildGame.invalid.androidLabel", "Android application label must contain 1-128 printable characters.") } };
    }
    if (spec->target == kb::packaging::PackagingTarget::WindowsX64 && !Trimmed(project.crashReportUploadUrl).empty() &&
        !kb::platform::IsAllowedCrashUploadEndpoint(Trimmed(project.crashReportUploadUrl)))
        return { false, std::string{ EditorText("buildGame.invalid.crashReportUrl", "Crash report URL must use HTTPS, or HTTP to 127.0.0.1 or ::1.") } };
    const EditorBuildGameTargetSettings& targetSettings = local.For(target);
    if (release && spec->needsAndroidMetadata) {
        if (targetSettings.androidKeystore.empty()) return { false, Missing(EditorText("buildGame.required.androidKeystore", "Android keystore")) };
        if (!package_input::IsValidAndroidKeyAlias(Trimmed(targetSettings.androidKeyAlias)))
            return { false, std::string{ EditorText("buildGame.invalid.androidKeyAlias",
                "Android key alias must use 1-128 ASCII letters, digits, dots, dashes, or underscores.") } };
        if (!hasStorePassword) return { false, Missing(EditorText("buildGame.required.androidKeystorePassword", "Android keystore password")) };
        if (!hasKeyPassword) return { false, Missing(EditorText("buildGame.required.androidKeyPassword", "Android key password")) };
    }
    if (release && spec->target == kb::packaging::PackagingTarget::WindowsX64) {
        if (!targetSettings.windowsCertificateThumbprint.empty() && !targetSettings.windowsCertificateFile.empty())
            return { false, std::string{ EditorText("buildGame.invalid.certificateBoth", "Sign with a certificate thumbprint or a certificate file, not both.") } };
        if (!targetSettings.windowsCertificateThumbprint.empty() &&
            !package_input::IsValidCertificateThumbprint(targetSettings.windowsCertificateThumbprint))
            return { false, std::string{ EditorText("buildGame.invalid.thumbprint", "Certificate thumbprint must be 40 hexadecimal digits.") } };
        if (!targetSettings.windowsCertificateFile.empty() && !hasCertificatePassword)
            return { false, Missing(EditorText("buildGame.field.certificatePassword", "Certificate password")) };
        if (!targetSettings.windowsTimestampUrl.empty() && !package_input::IsValidTimestampUrl(targetSettings.windowsTimestampUrl))
            return { false, std::string{ EditorText("buildGame.invalid.timestampUrl", "Timestamp URL must be an http:// or https:// address.") } };
    }
    if (targetSettings.outputDirectory.empty()) return { false, Missing(EditorText("buildGame.field.outputDirectory", "Output directory")) };
    if (local.builderExecutable.empty()) return { false, Missing(EditorText("buildGame.field.builderExecutable", "Builder executable")) };
    if (local.buildRoot.empty()) return { false, Missing(EditorText("buildGame.field.buildDirectory", "Build directory")) };
    if (spec->family == kb::packaging::PackagingTargetFamily::Web && local.emsdkRoot.empty())
        return { false, Missing(EditorText("buildGame.field.emscriptenSDK", "Emscripten SDK")) };
    if (spec->target == kb::packaging::PackagingTarget::LinuxX64) {
        if (!package_input::IsValidLinuxHost(Trimmed(local.linuxHost))) {
            return { false, std::string{ EditorText("buildGame.invalid.linuxHost", "Linux SSH host is invalid.") } };
        }
        if (!package_input::IsValidLinuxUser(Trimmed(local.linuxUser))) {
            return { false, std::string{ EditorText("buildGame.invalid.linuxUser", "Linux SSH user is invalid.") } };
        }
        if (!package_input::IsValidLinuxHostKey(Trimmed(local.linuxHostKey)))
            return { false, std::string{ EditorText("buildGame.invalid.linuxHostKey", "Linux host key must contain an allowlisted type and base64 public key.") } };
        if (local.linuxPort == 0U) return { false, std::string{ EditorText("buildGame.invalid.linuxPort", "Linux SSH port is invalid.") } };
        if (!package_input::IsValidLinuxEngineRoot(Trimmed(local.linuxEngineRoot)))
            return { false, std::string{ EditorText("buildGame.invalid.linuxEngineRoot", "Linux engine root must be a safe absolute POSIX path.") } };
        if (!package_input::IsValidLinuxDisplay(Trimmed(local.linuxDisplay))) {
            return { false, std::string{ EditorText("buildGame.invalid.linuxDisplay", "Linux X11 display is invalid.") } };
        }
    }
    return { true, EditorLocalization::Format("buildGame.ready", "Ready to build {target}.",
        { { "target", std::string{ TargetDisplayName(spec->target) } } }) };
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
    case BuildGameField::WindowsCertificateThumbprint: {
        std::string thumbprint;
        for (const char character : text) {
            if (character != ' ' && character != ':') thumbprint.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(character))));
        }
        if (!thumbprint.empty() && !package_input::IsValidCertificateThumbprint(thumbprint)) {
            error = "Certificate thumbprint must be 40 hexadecimal digits.";
            return false;
        }
        local.For(target).windowsCertificateThumbprint = thumbprint;
        return true;
    }
    case BuildGameField::WindowsTimestampUrl:
        if (!text.empty() && !package_input::IsValidTimestampUrl(text)) {
            error = "Timestamp URL must be an http:// or https:// address.";
            return false;
        }
        local.For(target).windowsTimestampUrl = text;
        return true;
    case BuildGameField::CrashReportUrl:
        if (!text.empty() && !kb::platform::IsAllowedCrashUploadEndpoint(text)) {
            error = "Crash report URL must use HTTPS, or HTTP to 127.0.0.1 or ::1.";
            return false;
        }
        project.crashReportUploadUrl = text;
        return true;
    case BuildGameField::None:
    case BuildGameField::ProjectName:
    case BuildGameField::ApplicationIcon:
    case BuildGameField::AndroidKeystore:
    case BuildGameField::AndroidStorePassword:
    case BuildGameField::AndroidKeyPassword:
    case BuildGameField::WindowsCertificateFile:
    case BuildGameField::WindowsCertificatePassword:
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
