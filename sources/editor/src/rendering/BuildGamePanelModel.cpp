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

// What players see: the name in the window title, the Start menu and the store, who made it, which version it is and
// the line of rights it carries. Every one of these is plain text in any language.
constexpr std::array<BuildGameRowSpec, 5> kGameRows{{
    { BuildGameField::ProductName, [] { return EditorText("buildGame.field.gameName", "Game name"); }, BuildGameRowKind::Text, true },
    { BuildGameField::Publisher, [] { return EditorText("buildGame.field.studio", "Studio or author"); }, BuildGameRowKind::Text, true },
    { BuildGameField::Version, [] { return EditorText("buildGame.field.version", "Version"); }, BuildGameRowKind::Text, true },
    { BuildGameField::Description, [] { return EditorText("buildGame.field.description", "Description"); }, BuildGameRowKind::Text, false },
    { BuildGameField::Copyright, [] { return EditorText("buildGame.field.copyright", "Copyright"); }, BuildGameRowKind::Text, false },
}};
constexpr std::array<BuildGameRowSpec, 2> kAppearanceRows{{
    { BuildGameField::ApplicationIcon, [] { return EditorText("buildGame.field.applicationIcon", "Application icon"); }, BuildGameRowKind::IconPicker, false },
    { BuildGameField::LoadingScreenImage, [] { return EditorText("buildGame.field.loadingScreen", "Loading screen image"); }, BuildGameRowKind::ImagePicker, false },
}};
// The store identity of an Android app; the name and icon are the game's own.
constexpr std::array<BuildGameRowSpec, 4> kAndroidApplicationRows{{
    { BuildGameField::AndroidFormat, [] { return EditorText("buildGame.field.androidFormat", "Package format (apk/aab)"); }, BuildGameRowKind::Choice, true },
    { BuildGameField::AndroidApplicationId, [] { return EditorText("buildGame.field.applicationId", "Application id"); }, BuildGameRowKind::Text, true },
    { BuildGameField::AndroidVersionCode, [] { return EditorText("buildGame.field.versionCode", "Version code"); }, BuildGameRowKind::Text, true },
    { BuildGameField::AndroidLabel, [] { return EditorText("buildGame.field.applicationLabel", "Application label"); }, BuildGameRowKind::Text, true },
}};
constexpr std::array<BuildGameRowSpec, 1> kContentRows{{
    { BuildGameField::StartupMap, [] { return EditorText("buildGame.field.startupMap", "Startup map"); }, BuildGameRowKind::ScenePicker, true },
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
constexpr std::array<BuildGameRowSpec, 2> kOutputRows{{
    { BuildGameField::OutputDirectory, [] { return EditorText("buildGame.field.outputDirectory", "Output directory"); }, BuildGameRowKind::FolderPicker, true },
    { BuildGameField::LaunchAfterBuild, [] { return EditorText("buildGame.field.launchAfterBuild", "Launch after build"); }, BuildGameRowKind::Checkbox, false },
}};
// Filled in for the user: the file name comes from the game name and the engine build from the editor that runs.
constexpr std::array<BuildGameRowSpec, 3> kAdvancedRows{{
    { BuildGameField::ExecutableName, [] { return EditorText("buildGame.field.executableName", "Executable name"); }, BuildGameRowKind::Text, true },
    { BuildGameField::BuilderExecutable, [] { return EditorText("buildGame.field.builderExecutable", "Builder executable"); }, BuildGameRowKind::Text, true },
    { BuildGameField::BuildRoot, [] { return EditorText("buildGame.field.buildDirectory", "Build directory"); }, BuildGameRowKind::FolderPicker, true },
}};
// The Windows player is the one host with crash reports, so only it offers an endpoint.
constexpr std::array<BuildGameRowSpec, 4> kWindowsAdvancedRows{{
    { BuildGameField::ExecutableName, [] { return EditorText("buildGame.field.executableName", "Executable name"); }, BuildGameRowKind::Text, true },
    { BuildGameField::CrashReportUrl, [] { return EditorText("buildGame.field.crashReportURL", "Crash report URL"); }, BuildGameRowKind::Text, false },
    { BuildGameField::BuilderExecutable, [] { return EditorText("buildGame.field.builderExecutable", "Builder executable"); }, BuildGameRowKind::Text, true },
    { BuildGameField::BuildRoot, [] { return EditorText("buildGame.field.buildDirectory", "Build directory"); }, BuildGameRowKind::FolderPicker, true },
}};
constexpr std::array<BuildGameRowSpec, 3> kWebAdvancedRows{{
    { BuildGameField::ExecutableName, [] { return EditorText("buildGame.field.bundleName", "Bundle name"); }, BuildGameRowKind::Text, true },
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

// Latin letters with marks, as the plain letters a file name can carry; other characters are left out.
[[nodiscard]] std::string_view PlainLetters(char32_t codePoint) noexcept {
    switch (codePoint) {
    case U'\u0105': case U'\u00E1': case U'\u00E0': case U'\u00E2': case U'\u00E4': case U'\u00E3': case U'\u00E5': case U'\u0103': return "a";
    case U'\u0104': case U'\u00C1': case U'\u00C0': case U'\u00C2': case U'\u00C4': case U'\u00C3': case U'\u00C5': case U'\u0102': return "A";
    case U'\u0107': case U'\u010D': case U'\u00E7': return "c";
    case U'\u0106': case U'\u010C': case U'\u00C7': return "C";
    case U'\u010F': return "d";
    case U'\u010E': return "D";
    case U'\u0119': case U'\u00E9': case U'\u00E8': case U'\u00EA': case U'\u00EB': case U'\u011B': return "e";
    case U'\u0118': case U'\u00C9': case U'\u00C8': case U'\u00CA': case U'\u00CB': case U'\u011A': return "E";
    case U'\u00ED': case U'\u00EC': case U'\u00EE': case U'\u00EF': return "i";
    case U'\u00CD': case U'\u00CC': case U'\u00CE': case U'\u00CF': return "I";
    case U'\u0142': return "l";
    case U'\u0141': return "L";
    case U'\u0144': case U'\u00F1': case U'\u0148': return "n";
    case U'\u0143': case U'\u00D1': case U'\u0147': return "N";
    case U'\u00F3': case U'\u00F2': case U'\u00F4': case U'\u00F6': case U'\u00F5': case U'\u00F8': case U'\u0151': return "o";
    case U'\u00D3': case U'\u00D2': case U'\u00D4': case U'\u00D6': case U'\u00D5': case U'\u00D8': case U'\u0150': return "O";
    case U'\u0159': return "r";
    case U'\u0158': return "R";
    case U'\u015B': case U'\u0161': case U'\u0219': case U'\u015F': return "s";
    case U'\u015A': case U'\u0160': case U'\u0218': case U'\u015E': return "S";
    case U'\u00DF': return "ss";
    case U'\u0165': case U'\u021B': return "t";
    case U'\u0164': case U'\u021A': return "T";
    case U'\u00FA': case U'\u00F9': case U'\u00FB': case U'\u00FC': case U'\u016F': case U'\u0171': return "u";
    case U'\u00DA': case U'\u00D9': case U'\u00DB': case U'\u00DC': case U'\u016E': case U'\u0170': return "U";
    case U'\u00FD': case U'\u00FF': return "y";
    case U'\u00DD': return "Y";
    case U'\u017A': case U'\u017C': case U'\u017E': return "z";
    case U'\u0179': case U'\u017B': case U'\u017D': return "Z";
    default: return {};
    }
}

// The next code point of UTF-8 text, or U+FFFD for a broken sequence; `index` moves past it.
[[nodiscard]] char32_t NextCodePoint(std::string_view text, std::size_t& index) noexcept {
    const unsigned char lead = static_cast<unsigned char>(text[index++]);
    if (lead < 0x80U) return lead;
    const int extra = lead >= 0xF0U ? 3 : lead >= 0xE0U ? 2 : lead >= 0xC0U ? 1 : -1;
    if (extra < 0) return U'\uFFFD';
    char32_t value = lead & (0x3FU >> extra);
    for (int count = 0; count < extra; ++count) {
        if (index >= text.size() || (static_cast<unsigned char>(text[index]) & 0xC0U) != 0x80U) return U'\uFFFD';
        value = (value << 6U) | (static_cast<unsigned char>(text[index++]) & 0x3FU);
    }
    return value;
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
    const bool web = targetSpec->family == kb::packaging::PackagingTargetFamily::Web;
    std::vector<BuildGameSectionSpec> sections{
        { BuildGameSection::Game, EditorText("buildGame.section.game", "GAME"), kGameRows },
        { BuildGameSection::Appearance, EditorText("buildGame.section.appearance", "APPEARANCE"), kAppearanceRows },
        { BuildGameSection::Content, EditorText("buildGame.section.content", "CONTENT"), kContentRows },
    };
    if (targetSpec->needsAndroidMetadata)
        sections.push_back({ BuildGameSection::Application, EditorText("buildGame.section.androidApplication", "ANDROID APPLICATION"), kAndroidApplicationRows });
    if (targetSpec->needsAndroidMetadata && release)
        sections.push_back({ BuildGameSection::Signing, EditorText("buildGame.section.signing", "SIGNING"), kReleaseSigningRows });
    if (target == kb::packaging::PackagingTarget::WindowsX64 && release)
        sections.push_back({ BuildGameSection::Signing, EditorText("buildGame.section.signing", "SIGNING"), kWindowsSigningRows });
    if (web)
        sections.push_back({ BuildGameSection::Toolchain, EditorText("buildGame.section.webToolchain", "WEB TOOLCHAIN"), kWebToolchainRows });
    if (targetSpec->target == kb::packaging::PackagingTarget::LinuxX64)
        sections.push_back({ BuildGameSection::Toolchain, EditorText("buildGame.section.linuxBuildHost", "LINUX BUILD HOST"), kLinuxToolchainRows });
    sections.push_back({ BuildGameSection::Output, EditorText("buildGame.section.output", "OUTPUT"), kOutputRows });
    sections.push_back({ BuildGameSection::Advanced, EditorText("buildGame.section.advanced", "ADVANCED"),
        target == kb::packaging::PackagingTarget::WindowsX64 ? std::span<const BuildGameRowSpec>{ kWindowsAdvancedRows }
        : web ? std::span<const BuildGameRowSpec>{ kWebAdvancedRows } : std::span<const BuildGameRowSpec>{ kAdvancedRows } });
    return sections;
}

std::string BuildGamePanelModel::ShippedGameName(const kb::project::ProjectSettings& project) {
    const std::string game = Trimmed(project.gameName);
    return game.empty() ? Trimmed(project.name) : game;
}

std::string BuildGamePanelModel::ShippedAndroidApplicationId(const kb::project::ProjectSettings& project) {
    const std::string chosen = Trimmed(project.androidApplicationId);
    if (!chosen.empty()) return chosen;
    // One segment from a name: its plain letters and digits, lower case, starting with a letter.
    const auto segment = [](std::string_view name) {
        std::string plain;
        for (std::size_t index = 0U; index < name.size();) {
            const char32_t codePoint = NextCodePoint(name, index);
            if (codePoint < 0x80U) {
                const char character = static_cast<char>(codePoint);
                if (package_input::IsAsciiAlphaNumeric(character)) plain.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(character))));
            } else {
                for (const char character : PlainLetters(codePoint)) plain.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(character))));
            }
        }
        std::size_t first = 0U;
        while (first < plain.size() && std::isalpha(static_cast<unsigned char>(plain[first])) == 0) ++first;
        plain.erase(0U, first);
        if (plain.size() > 40U) plain.resize(40U);
        return plain;
    };
    std::string studio = segment(project.publisher);
    std::string game = segment(ShippedFileName(project));
    if (studio.empty()) studio = "studio";
    if (game.empty()) game = "game";
    return "com." + studio + "." + game;
}

std::string BuildGamePanelModel::ShippedAndroidLabel(const kb::project::ProjectSettings& project) {
    const std::string chosen = Trimmed(project.androidLabel);
    return chosen.empty() ? ShippedGameName(project) : chosen;
}

std::string_view BuildGamePanelModel::ChoiceLabel(BuildGameField field, std::string_view value) {
    if (field == BuildGameField::AndroidFormat) {
        return value == "aab" ? EditorText("buildGame.value.formatAab", "AAB - for Google Play")
                              : EditorText("buildGame.value.formatApk", "APK - install directly on a device");
    }
    return value;
}

std::string BuildGamePanelModel::ShippedFileName(const kb::project::ProjectSettings& project) {
    const std::string chosen = Trimmed(project.executableName);
    if (!chosen.empty()) return chosen;
    const std::string game = ShippedGameName(project);
    std::string name;
    for (std::size_t index = 0U; index < game.size() && name.size() < 80U;) {
        const char32_t codePoint = NextCodePoint(game, index);
        if (codePoint < 0x80U) {
            const char character = static_cast<char>(codePoint);
            if (package_input::IsAsciiAlphaNumeric(character) || character == '_' || character == '-' ||
                character == '.' || character == ' ') {
                name.push_back(character);
            }
        } else {
            name.append(PlainLetters(codePoint));
        }
    }
    // A file name starts with a letter or digit and does not end in a dot or space.
    std::size_t first = 0U;
    while (first < name.size() && !package_input::IsAsciiAlphaNumeric(name[first])) ++first;
    name.erase(0U, first);
    if (name.size() > 80U) name.resize(80U);
    while (!name.empty() && (name.back() == '.' || name.back() == ' ')) name.pop_back();
    return name.empty() || !package_input::IsValidPackageName(name, true) ? std::string{ "Game" } : name;
}

std::string BuildGamePanelModel::Value(BuildGameField field, kb::packaging::PackagingTarget target,
    const kb::project::ProjectSettings& project, const EditorBuildGameSettings& local) {
    if (TargetSpec(target) == nullptr) return {};
    const EditorBuildGameTargetSettings& targetSettings = local.For(target);
    switch (field) {
    case BuildGameField::Publisher: return project.publisher;
    case BuildGameField::Version: return project.version;
    case BuildGameField::ProductName: return ShippedGameName(project);
    case BuildGameField::ExecutableName: return ShippedFileName(project);
    case BuildGameField::Description: return project.description;
    case BuildGameField::Copyright: return project.copyright;
    case BuildGameField::ApplicationIcon: return project.applicationIcon;
    case BuildGameField::LoadingScreenImage: return project.display.splash.image;
    case BuildGameField::StartupMap: return project.defaultMap;
    case BuildGameField::AndroidApplicationId: return ShippedAndroidApplicationId(project);
    case BuildGameField::AndroidVersionCode: return std::to_string(project.androidVersionCode);
    case BuildGameField::AndroidLabel: return ShippedAndroidLabel(project);
    case BuildGameField::AndroidKeystore: return targetSettings.androidKeystore.generic_string();
    case BuildGameField::AndroidKeyAlias: return targetSettings.androidKeyAlias;
    case BuildGameField::AndroidFormat: return std::string{ ChoiceLabel(field, targetSettings.androidFormat) };
    case BuildGameField::AndroidStorePassword:
    case BuildGameField::AndroidKeyPassword: return {};
    case BuildGameField::OutputDirectory: return targetSettings.outputDirectory.generic_string();
    case BuildGameField::LaunchAfterBuild: return targetSettings.launchAfterBuild ? "true" : "false";
    case BuildGameField::BuilderExecutable: return local.builderExecutable.generic_string();
    case BuildGameField::BuildRoot: return local.buildRoot.generic_string();
    case BuildGameField::EmsdkRoot: return local.emsdkRoot.generic_string();
    case BuildGameField::LinuxHost: return local.linuxHost;
    case BuildGameField::LinuxUser: return local.linuxUser;
    case BuildGameField::LinuxHostKey: return local.linuxHostKey;
    case BuildGameField::LinuxPort: return std::to_string(local.linuxPort);
    case BuildGameField::LinuxEngineRoot: return local.linuxEngineRoot;
    case BuildGameField::LinuxDisplay: return local.linuxDisplay;
    case BuildGameField::LinuxIdentity: return local.linuxIdentity.generic_string();
    case BuildGameField::CrashReportUrl: return project.crashReportUploadUrl;
    case BuildGameField::WindowsCertificateThumbprint: return targetSettings.windowsCertificateThumbprint;
    case BuildGameField::WindowsCertificateFile: return targetSettings.windowsCertificateFile.generic_string();
    case BuildGameField::WindowsCertificatePassword: return {};
    case BuildGameField::WindowsTimestampUrl: return targetSettings.windowsTimestampUrl;
    case BuildGameField::None: break;
    }
    return {};
}

std::string_view BuildGamePanelModel::Hint(BuildGameField field) {
    switch (field) {
    case BuildGameField::Publisher: return EditorText("buildGame.hint.studio", "Your studio or your own name");
    case BuildGameField::Description: return EditorText("buildGame.hint.description", "Optional - one sentence about the game");
    case BuildGameField::Copyright: return EditorText("buildGame.hint.copyright", "Optional - e.g. (c) 2026 Your Studio");
    case BuildGameField::ApplicationIcon: return EditorText("buildGame.value.selectPng", "Optional - select PNG...");
    case BuildGameField::LoadingScreenImage: return EditorText("buildGame.hint.loadingScreen", "Engine logo - select PNG or JPG...");
    case BuildGameField::AndroidKeystore:
    case BuildGameField::AndroidKeyAlias: return EditorText("buildGame.value.developmentOnly", "Development only");
    case BuildGameField::AndroidStorePassword:
    case BuildGameField::AndroidKeyPassword:
    case BuildGameField::WindowsCertificatePassword: return EditorText("buildGame.value.oneBuildOnly", "Entered for one build only");
    case BuildGameField::OutputDirectory: return EditorText("buildGame.value.selectFolder", "Select folder...");
    case BuildGameField::BuildRoot: return EditorText("buildGame.value.selectBuildDirectory", "Select build directory...");
    case BuildGameField::EmsdkRoot: return EditorText("buildGame.value.selectEmsdk", "Select emsdk...");
    case BuildGameField::LinuxIdentity: return EditorText("buildGame.value.selectPrivateKey", "Optional - select private key...");
    case BuildGameField::CrashReportUrl: return EditorText("buildGame.value.crashReportsLocal", "Optional - players' crash reports stay local");
    case BuildGameField::WindowsCertificateThumbprint: return EditorText("buildGame.value.thumbprintHint", "Optional - SHA-1 of a certificate in your store");
    case BuildGameField::WindowsCertificateFile: return EditorText("buildGame.value.selectPfx", "Optional - select PFX...");
    case BuildGameField::WindowsTimestampUrl: return EditorText("buildGame.value.timestampHint", "Optional - RFC 3161 timestamp service");
    default: return {};
    }
}

BuildGameValidation BuildGamePanelModel::Validate(kb::packaging::PackagingTarget target,
    const kb::project::ProjectSettings& project, const EditorBuildGameSettings& local,
    bool release, bool jobRunning, bool hasStorePassword, bool hasKeyPassword, bool hasCertificatePassword) {
    if (jobRunning) return { false, std::string{ EditorText("buildGame.invalid.jobRunning", "A package job is running.") } };
    if (!ValidText(ShippedGameName(project), 128U)) {
        return { false, std::string{ EditorText("buildGame.invalid.gameName", "Give the game a name (up to 128 characters) in Game name.") } };
    }
    if (!ValidText(Trimmed(project.publisher), 128U)) {
        return { false, std::string{ EditorText("buildGame.invalid.studio", "Type your studio or your own name in Studio or author.") } };
    }
    if (project.description.size() > 512U || project.copyright.size() > 256U) {
        return { false, std::string{ EditorText("buildGame.invalid.longText", "Description and copyright must be shorter.") } };
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
    const std::string packageName = ShippedFileName(project);
    if (!package_input::IsValidPackageName(
            packageName, spec->target == kb::packaging::PackagingTarget::WindowsX64))
        return { false, std::string{ EditorText("buildGame.invalid.executableName",
            "Executable or package name must use 1-80 ASCII letters, digits, spaces, dots, dashes, or underscores.") } };
    if (spec->needsAndroidMetadata) {
        if (!ValidAndroidApplicationId(ShippedAndroidApplicationId(project)))
            return { false, std::string{ EditorText("buildGame.invalid.androidApplicationId", "Android application id must be a lowercase dotted identifier.") } };
        if (project.androidVersionCode == 0U || project.androidVersionCode > 2'100'000'000U)
            return { false, std::string{ EditorText("buildGame.invalid.androidVersionCode", "Android version code must be between 1 and 2100000000.") } };
        if (!ValidText(ShippedAndroidLabel(project), 128U))
            return { false, std::string{ EditorText("buildGame.invalid.androidLabel", "Android application label must contain 1-128 printable characters.") } };
    }
    if (spec->target == kb::packaging::PackagingTarget::WindowsX64 && !Trimmed(project.crashReportUploadUrl).empty() &&
        !kb::platform::IsAllowedCrashUploadEndpoint(Trimmed(project.crashReportUploadUrl)))
        return { false, std::string{ EditorText("buildGame.invalid.crashReportUrl", "Crash report URL must use HTTPS, or HTTP to 127.0.0.1 or ::1.") } };
    const EditorBuildGameTargetSettings& targetSettings = local.For(target);
    if (spec->needsAndroidMetadata) {
        if (targetSettings.androidFormat != "apk" && targetSettings.androidFormat != "aab")
            return {false, "Android package format must be apk or aab."};
    }
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
    case BuildGameField::Description:
        if (text.size() > 512U) { error = "Description must be at most 512 bytes."; return false; }
        project.description = text; return true;
    case BuildGameField::Copyright:
        if (text.size() > 256U) { error = "Copyright must be at most 256 bytes."; return false; }
        project.copyright = text; return true;
    case BuildGameField::Version:
        if (!ValidVersion(text)) { error = "Version must contain 1-64 printable characters."; return false; }
        project.version = text; return true;
    case BuildGameField::ProductName:
        if (!ValidText(text, 128U)) { error = "Game name must contain 1-128 characters."; return false; }
    {
        // An executable name or Android label that was only the one made from the game name follows the new name;
        // one the user chose stays.
        kb::project::ProjectSettings automatic = project;
        automatic.executableName.clear();
        const bool followsGameName = Trimmed(project.executableName) == ShippedFileName(automatic);
        // The Android label a new project starts with is its name; like the file name, it follows the game name.
        const bool labelFollows = Trimmed(project.androidLabel) == ShippedGameName(project);
        // The project's own name is the default, so typing it back leaves the game name unset.
        project.gameName = text == Trimmed(project.name) ? std::string{} : text;
        if (followsGameName) project.executableName.clear();
        if (labelFollows) project.androidLabel.clear();
        return true;
    }
    case BuildGameField::ExecutableName:
        // Cleared, the name is made from the game name again.
        if (text.empty()) { project.executableName.clear(); return true; }
        if (!package_input::IsValidPackageName(
                text, target == kb::packaging::PackagingTarget::WindowsX64)) {
            error = "Executable name is not valid for the selected target.";
            return false;
        }
        project.executableName = text; return true;
    case BuildGameField::StartupMap: {
        // The startup scene leads the packaged scenes, as when the Scenes page chooses it, so it is not listed
        // twice. An empty entry is left for Validate to report.
        if (text.empty()) { project.defaultMap = text; return true; }
        std::vector<std::string> scenes{ text };
        scenes.insert(scenes.end(), project.additionalScenes.begin(), project.additionalScenes.end());
        kb::project::SetProjectBuildScenes(project, std::move(scenes));
        return true;
    }
    case BuildGameField::AndroidApplicationId:
        // Cleared, the id is made from the studio and game names again.
        if (text.empty()) { project.androidApplicationId.clear(); return true; }
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
    case BuildGameField::AndroidLabel:
        if (text.size() > 128U) { error = "Android application label must be at most 128 characters."; return false; }
        project.androidLabel = text == ShippedGameName(project) ? std::string{} : text;
        return true;
    case BuildGameField::AndroidKeyAlias: local.For(target).androidKeyAlias = text; return true;
    case BuildGameField::AndroidFormat:
        if (text != "apk" && text != "aab") {
            error = "Android package format must be apk or aab.";
            return false;
        }
        local.For(target).androidFormat = text;
        return true;
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
    case BuildGameField::ApplicationIcon:
    case BuildGameField::LoadingScreenImage:
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
    // Whole characters of any language; control characters and broken sequences are left out.
    std::string printable;
    printable.reserve(std::min(text.size(), maximumLength));
    for (std::size_t index = 0U; index < text.size();) {
        const std::size_t start = index;
        const char32_t codePoint = NextCodePoint(text, index);
        if (codePoint < 32U || codePoint == 0x7FU || codePoint == U'\uFFFD' || (codePoint >= 0x80U && codePoint < 0xA0U)) continue;
        printable.append(text.substr(start, index - start));
    }
    if (printable.empty()) return false;
    if (selectAll) {
        buffer.clear();
        selectAll = false;
    }
    if (buffer.size() >= maximumLength) return false;
    // Cut at a character boundary, never inside one.
    std::size_t room = std::min(printable.size(), maximumLength - buffer.size());
    while (room > 0U && room < printable.size() && (static_cast<unsigned char>(printable[room]) & 0xC0U) == 0x80U) --room;
    buffer.append(printable, 0U, room);
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
