#include "packaging/EditorPackageRequestBuilder.hpp"

#include "packaging/EditorPackageInputValidation.hpp"
#include "rendering/BuildGamePanelModel.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <system_error>

namespace kb::editor {
namespace {

[[nodiscard]] std::filesystem::path AbsoluteNormalized(const std::filesystem::path& path) {
    std::error_code error;
    const std::filesystem::path absolute = std::filesystem::absolute(path, error);
    return (error ? path : absolute).lexically_normal();
}

[[nodiscard]] std::string SafeUnitComponent(std::string_view value, bool windowsTarget) {
    if (windowsTarget && package_input::IsWindowsReservedDeviceName(value)) return {};
    std::string result;
    result.reserve(value.size());
    for (const char character : value) {
        const unsigned char byte = static_cast<unsigned char>(character);
        if (std::isalnum(byte) != 0 || character == '-' || character == '_') result.push_back(character);
        else if ((character == ' ' || character == '.') && !result.empty() && result.back() != '_') result.push_back('_');
        else return {};
    }
    while (!result.empty() && result.back() == '_') result.pop_back();
    return result;
}

[[nodiscard]] std::string Trimmed(std::string_view value) {
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front())) != 0) value.remove_prefix(1U);
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back())) != 0) value.remove_suffix(1U);
    return std::string{ value };
}

} // namespace

bool BuildPackageRequest(const EditorPackageRequestInput& input, EditorPackageRequest& request, std::string& error) {
    const kb::project::ProjectSettings& project = input.project;
    const kb::packaging::PackagingTargetSpec& targetSpec = input.target;
    const EditorBuildGameTargetSettings& targetSettings = input.local.For(targetSpec.target);
    request = EditorPackageRequest{};
    request.builderExecutable = input.local.builderExecutable;
    request.packageScript = input.engineRoot / "scripts" / "package_game.py";
    request.projectFile = AbsoluteNormalized(input.projectFile);
    request.targetId = targetSpec.bakeId;
    request.configuration = input.release ? "Release" : "Development";
    const std::filesystem::path outputBase = AbsoluteNormalized(targetSettings.outputDirectory);
    const std::filesystem::path projectRoot = AbsoluteNormalized(input.projectRoot);
    const std::filesystem::path buildRoot = AbsoluteNormalized(input.local.buildRoot);
    if (package_input::IsPathInsideOrEqual(projectRoot, outputBase) || package_input::IsPathInsideOrEqual(projectRoot, buildRoot)) {
        error = "Package output and build directories must be outside the project.";
        return false;
    }
    const std::string unitComponent = SafeUnitComponent(
        BuildGamePanelModel::ShippedFileName(project), targetSpec.target == kb::packaging::PackagingTarget::WindowsX64);
    if (unitComponent.empty()) {
        error = "Product or executable name cannot form a safe package directory name.";
        return false;
    }
    request.outputDirectory = (outputBase / (unitComponent + '-' + request.targetId + '-' + request.configuration)).lexically_normal();
    if (request.outputDirectory.parent_path() != outputBase) {
        error = "Package directory escaped the selected output directory.";
        return false;
    }
    request.engineRoot = AbsoluteNormalized(input.engineRoot);
    request.buildRoot = buildRoot;
    request.productName = BuildGamePanelModel::ShippedGameName(project);
    request.publisher = Trimmed(project.publisher);
    request.version = Trimmed(project.version);
    request.description = Trimmed(project.description);
    request.copyright = Trimmed(project.copyright);
    request.executableName = BuildGamePanelModel::ShippedFileName(project);
    if (!project.applicationIcon.empty()) {
        const std::filesystem::path iconRelative{ project.applicationIcon };
        if (!package_input::IsNormalProjectRelativePath(iconRelative)) {
            error = "Application icon must be a normal project-relative PNG path.";
            return false;
        }
        request.applicationIcon = AbsoluteNormalized(projectRoot / iconRelative);
        if (!package_input::IsValidProjectPngIcon(request.projectFile, request.applicationIcon)) {
            error = "Application icon must be an existing PNG inside the project.";
            return false;
        }
    }
    if (!input.local.releaseSigningKey.empty()) {
        request.releaseSigningKey = AbsoluteNormalized(input.local.releaseSigningKey);
    }
    if (targetSpec.target == kb::packaging::PackagingTarget::WindowsX64) {
        request.crashReportUploadUrl = Trimmed(project.crashReportUploadUrl);
        if (input.release) {
            request.windowsCertificateThumbprint = targetSettings.windowsCertificateThumbprint;
            request.windowsCertificateFile = targetSettings.windowsCertificateFile;
            request.windowsTimestampUrl = targetSettings.windowsTimestampUrl;
            if (!request.windowsCertificateFile.empty()) request.windowsCertificatePassword = input.windowsCertificatePassword;
        }
    }
    if (targetSpec.needsAndroidMetadata) {
        request.androidApplicationId = BuildGamePanelModel::ShippedAndroidApplicationId(project);
        request.androidVersionCode = project.androidVersionCode;
        request.androidLabel = BuildGamePanelModel::ShippedAndroidLabel(project);
        request.androidMicrophone = project.microphone;
        request.androidNetwork = std::string{ kb::project::ProjectNetworkingModeName(project.networkingMode) };
        std::ranges::transform(request.androidNetwork, request.androidNetwork.begin(), [](char value) {
            return static_cast<char>(value >= 'A' && value <= 'Z' ? value + ('a' - 'A') : value);
        });
        for (const float channel : project.display.splash.backgroundColor) {
            static constexpr char kHex[] = "0123456789ABCDEF";
            const auto value = static_cast<unsigned>(std::lround(std::clamp(channel, 0.0F, 1.0F) * 255.0F));
            request.androidSplashBackground += kHex[value >> 4U];
            request.androidSplashBackground += kHex[value & 15U];
        }
        request.androidKeystore = targetSettings.androidKeystore;
        request.androidKeyAlias = Trimmed(targetSettings.androidKeyAlias);
        request.androidFormat = targetSettings.androidFormat;
        request.androidStorePassword = input.androidStorePassword;
        request.androidKeyPassword = input.androidKeyPassword;
    }
    if (targetSpec.family == kb::packaging::PackagingTargetFamily::Web) {
        request.emsdkRoot = AbsoluteNormalized(input.local.emsdkRoot);
    }
    if (targetSpec.target == kb::packaging::PackagingTarget::LinuxX64) {
        request.linuxHost = Trimmed(input.local.linuxHost);
        request.linuxUser = Trimmed(input.local.linuxUser);
        request.linuxHostKey = Trimmed(input.local.linuxHostKey);
        request.linuxPort = input.local.linuxPort;
        request.linuxEngineRoot = Trimmed(input.local.linuxEngineRoot);
        request.linuxDisplay = Trimmed(input.local.linuxDisplay);
        request.linuxIdentity = input.local.linuxIdentity.empty() ? std::filesystem::path{} : AbsoluteNormalized(input.local.linuxIdentity);
    }
    request.launch = targetSettings.launchAfterBuild;
    return true;
}

} // namespace kb::editor
