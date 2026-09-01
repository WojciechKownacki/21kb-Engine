#include "engine/packaging/PackagingTargetCatalog.hpp"
#include "engine/project/ProjectSettings.hpp"
#include "packaging/EditorPackageInputValidation.hpp"
#include "packaging/EditorPackageProcessEnvironment.hpp"
#include "packaging/EditorAndroidSigningBroker.hpp"
#include "packaging/EditorProjectIconTransaction.hpp"
#include "packaging/EditorProjectPackageService.hpp"
#include "rendering/BuildGamePanelLayout.hpp"
#include "rendering/BuildGamePanelModel.hpp"
#include "settings/EditorBuildGameSettingsStore.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <winioctl.h>
#endif

namespace {

void Require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

[[nodiscard]] bool CreateDirectoryLink(
    const std::filesystem::path& link,
    const std::filesystem::path& target,
    std::error_code& error) {
    error.clear();
    std::filesystem::create_directory_symlink(target, link, error);
    if (!error) return true;
#if defined(_WIN32)
    error.clear();
    if (!CreateDirectoryW(link.c_str(), nullptr)) {
        error = std::error_code{ static_cast<int>(GetLastError()), std::system_category() };
        return false;
    }
    struct JunctionData {
        DWORD tag;
        WORD dataLength;
        WORD reserved;
        WORD substituteOffset;
        WORD substituteLength;
        WORD printOffset;
        WORD printLength;
        wchar_t path[1];
    };
    const std::wstring print = std::filesystem::absolute(target).lexically_normal().wstring();
    const std::wstring substitute = L"\\??\\" + print;
    const std::size_t substituteBytes = substitute.size() * sizeof(wchar_t);
    const std::size_t printBytes = print.size() * sizeof(wchar_t);
    const std::size_t pathBytes = substituteBytes + sizeof(wchar_t) + printBytes + sizeof(wchar_t);
    const std::size_t totalBytes = offsetof(JunctionData, path) + pathBytes;
    std::vector<unsigned char> storage(totalBytes, 0U);
    auto* data = reinterpret_cast<JunctionData*>(storage.data());
    data->tag = IO_REPARSE_TAG_MOUNT_POINT;
    data->dataLength = static_cast<WORD>(totalBytes - 8U);
    data->substituteOffset = 0U;
    data->substituteLength = static_cast<WORD>(substituteBytes);
    data->printOffset = static_cast<WORD>(substituteBytes + sizeof(wchar_t));
    data->printLength = static_cast<WORD>(printBytes);
    std::memcpy(data->path, substitute.data(), substituteBytes);
    std::memcpy(reinterpret_cast<unsigned char*>(data->path) + data->printOffset, print.data(), printBytes);
    const HANDLE handle = CreateFileW(
        link.c_str(), GENERIC_WRITE, 0U, nullptr, OPEN_EXISTING,
        FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    DWORD returned = 0U;
    const bool linked = handle != INVALID_HANDLE_VALUE && DeviceIoControl(
        handle, FSCTL_SET_REPARSE_POINT, data, static_cast<DWORD>(totalBytes),
        nullptr, 0U, &returned, nullptr) != FALSE;
    const DWORD lastError = linked ? ERROR_SUCCESS : GetLastError();
    if (handle != INVALID_HANDLE_VALUE) CloseHandle(handle);
    if (!linked) {
        RemoveDirectoryW(link.c_str());
        error = std::error_code{ static_cast<int>(lastError), std::system_category() };
    }
    return linked;
#else
    return false;
#endif
}

[[nodiscard]] bool HasField(const std::vector<kb::editor::BuildGameSectionSpec>& sections,
    kb::editor::BuildGameField field) {
    for (const auto& section : sections)
        for (const auto& row : section.rows)
            if (row.field == field) return true;
    return false;
}

void TargetCatalogTest() {
    constexpr std::array<std::string_view, 6> expected{
        "Windows.x64", "Android.ASTC.arm64", "Android.ETC2.arm64",
        "Linux.x64", "WebGL.wasm32", "WebGPU.wasm32",
    };
    const auto targets = kb::packaging::PackagingTargets();
    Require(targets.size() == expected.size(), "Packaging catalog must contain exactly six targets");
    for (std::size_t index = 0U; index < expected.size(); ++index) {
        Require(targets[index].bakeId == expected[index], "Packaging target bake id mismatch");
        Require(targets[index].displayName.find("Server") == std::string_view::npos, "Server target leaked into player packaging");
    }
    Require(kb::packaging::FindPackagingTarget("missing") == nullptr, "Unknown target silently resolved");
}

void SchemaAndValidationTest() {
    using enum kb::packaging::PackagingTarget;
    using kb::editor::BuildGameField;
    const auto windows = kb::editor::BuildGamePanelModel::Sections(WindowsX64, false);
    Require(HasField(windows, BuildGameField::ExecutableName), "Windows schema misses executable name");
    Require(!HasField(windows, BuildGameField::AndroidApplicationId), "Windows schema exposes Android metadata");
    const auto androidRelease = kb::editor::BuildGamePanelModel::Sections(AndroidEtc2Arm64, true);
    Require(HasField(androidRelease, BuildGameField::AndroidApplicationId), "ETC2 schema misses Android metadata");
    Require(HasField(androidRelease, BuildGameField::AndroidStorePassword), "Android Release misses one-build password input");
    const auto androidDevelopment = kb::editor::BuildGamePanelModel::Sections(AndroidAstcArm64, false);
    Require(!HasField(androidDevelopment, BuildGameField::AndroidKeystore), "Android Development exposes unused keystore input");
    Require(!HasField(androidDevelopment, BuildGameField::AndroidKeyAlias), "Android Development exposes unused key-alias input");
    Require(!HasField(androidDevelopment, BuildGameField::AndroidStorePassword), "Android Development exposes Release password input");
    Require(!HasField(androidDevelopment, BuildGameField::AndroidKeyPassword), "Android Development exposes Release key-password input");
    const auto webGpu = kb::editor::BuildGamePanelModel::Sections(WebGpuWasm32, false);
    Require(HasField(webGpu, BuildGameField::ExecutableName), "WebGPU schema misses its bundle name");
    Require(HasField(webGpu, BuildGameField::EmsdkRoot), "WebGPU schema misses Emscripten SDK configuration");
    const auto linux = kb::editor::BuildGamePanelModel::Sections(LinuxX64, false);
    Require(HasField(linux, BuildGameField::LinuxHost) && HasField(linux, BuildGameField::LinuxHostKey) &&
        HasField(linux, BuildGameField::LinuxIdentity), "Linux schema misses SSH build-host configuration");
    Require(kb::editor::BuildGamePanelModel::Sections(static_cast<kb::packaging::PackagingTarget>(255), false).empty(),
        "Invalid target silently used another target schema");

    kb::project::ProjectSettings project;
    project.name = "Project";
    project.gameName = "Game";
    project.publisher = "Publisher";
    project.version = "1.2.3";
    project.executableName = "Game";
    project.androidApplicationId = "com.publisher.game";
    project.androidVersionCode = 7U;
    project.androidLabel = "Game";
    kb::editor::EditorBuildGameSettings local;
    local.builderExecutable = "python";
    local.buildRoot = "C:/Build";
    local.For(WindowsX64).outputDirectory = "C:/Output";
    local.For(AndroidAstcArm64).outputDirectory = "C:/Output";
    local.For(AndroidEtc2Arm64).outputDirectory = "C:/Output";
    local.For(AndroidEtc2Arm64).androidKeystore = "C:/Signing/release.jks";
    local.For(AndroidEtc2Arm64).androidKeyAlias = "release";
    local.For(WebGpuWasm32).outputDirectory = "C:/Output";
    local.For(WebGlWasm32).outputDirectory = "C:/Output";
    local.emsdkRoot = "C:/emsdk";
    local.For(LinuxX64).outputDirectory = "C:/Output";
    local.linuxHost = "builder.example";
    local.linuxUser = "builder";
    local.linuxHostKey = "ssh-ed25519 AAAAC3NzaC1lZDI1NTE5AAAAITest";
    local.linuxPort = 2222U;
    local.linuxEngineRoot = "/opt/21kb";
    local.linuxDisplay = ":1";
    Require(kb::editor::BuildGamePanelModel::Validate(WindowsX64, project, local, false, false).canBuild,
        "Configured Windows BUILD should be enabled");
    Require(!kb::editor::BuildGamePanelModel::Validate(AndroidEtc2Arm64, project, local, true, false, false, true).canBuild,
        "Android Release BUILD ignored a missing keystore password");
    Require(kb::editor::BuildGamePanelModel::Validate(AndroidEtc2Arm64, project, local, true, false, true, true).canBuild,
        "Configured Android Release BUILD should be enabled");
    Require(kb::editor::BuildGamePanelModel::Validate(WebGpuWasm32, project, local, false, false).canBuild,
        "Configured WebGPU BUILD should be enabled");
    Require(kb::editor::BuildGamePanelModel::Validate(LinuxX64, project, local, false, false).canBuild,
        "Configured Linux BUILD should be enabled");
    project.version = "1.2.3-beta.1";
    Require(kb::editor::BuildGamePanelModel::Validate(WindowsX64, project, local, false, false).canBuild,
        "UI rejected a prerelease version supported by the package builder");
    project.version = "1.2.";
    Require(!kb::editor::BuildGamePanelModel::Validate(WindowsX64, project, local, false, false).canBuild,
        "UI accepted a Windows version with an empty trailing component");
    project.version = "1.-beta";
    Require(!kb::editor::BuildGamePanelModel::Validate(WindowsX64, project, local, false, false).canBuild,
        "UI accepted a Windows prerelease after an empty numeric component");
    project.version = "foo";
    Require(!kb::editor::BuildGamePanelModel::Validate(WindowsX64, project, local, false, false).canBuild,
        "UI accepted a Windows version rejected by PE resource generation");
    project.version = "65536.1";
    Require(!kb::editor::BuildGamePanelModel::Validate(WindowsX64, project, local, false, false).canBuild,
        "UI accepted a Windows version component above 65535");
    project.version = "1.2.3";
    project.gameName = std::string(129U, 'G');
    Require(!kb::editor::BuildGamePanelModel::Validate(WebGlWasm32, project, local, false, false).canBuild,
        "UI accepted a product name rejected by the package builder");
    project.gameName = "Game";
    project.publisher = std::string(129U, 'P');
    Require(!kb::editor::BuildGamePanelModel::Validate(AndroidAstcArm64, project, local, false, false).canBuild,
        "UI accepted a publisher rejected by the package builder");
    project.publisher = "Publisher";
    project.androidLabel = std::string(129U, 'L');
    Require(!kb::editor::BuildGamePanelModel::Validate(AndroidAstcArm64, project, local, false, false).canBuild,
        "UI accepted an Android label rejected by the package builder");
    project.androidLabel = "Game";
    project.androidVersionCode = 2'100'000'001U;
    Require(!kb::editor::BuildGamePanelModel::Validate(AndroidAstcArm64, project, local, false, false).canBuild,
        "UI accepted an Android version code rejected by the package builder");
    project.androidVersionCode = 7U;
    local.linuxHostKey = "builder.example ssh-ed25519 AAAAC3NzaC1lZDI1NTE5AAAAITest";
    Require(!kb::editor::BuildGamePanelModel::Validate(LinuxX64, project, local, false, false).canBuild,
        "UI accepted a host-key form rejected by the package builder");
    local.linuxHostKey = "ssh-ed25519 AAAAC3NzaC1lZDI1NTE5AAAAITest";
    local.For(AndroidEtc2Arm64).androidKeyAlias = "release alias";
    Require(!kb::editor::BuildGamePanelModel::Validate(AndroidEtc2Arm64, project, local, true, false, true, true).canBuild,
        "UI accepted an Android key alias rejected by the signing broker");
    local.For(AndroidEtc2Arm64).androidKeyAlias = "release";
    project.executableName.clear();
    Require(kb::editor::BuildGamePanelModel::Validate(AndroidAstcArm64, project, local, false, false).canBuild,
        "Android validation incorrectly required the hidden executable-name field");
    project.executableName = std::string(81U, 'A');
    Require(!kb::editor::BuildGamePanelModel::Validate(WindowsX64, project, local, false, false).canBuild,
        "UI accepted an executable name rejected by the package builder");
    project.executableName = "Game.";
    Require(!kb::editor::BuildGamePanelModel::Validate(WebGpuWasm32, project, local, false, false).canBuild,
        "UI accepted a trailing dot rejected by the package builder");
    constexpr std::array<std::string_view, 12U> windowsReservedNames{
        "CON", "prn.exe", "Aux.data", "nul", "COM1", "com9.exe",
        "LPT1", "lpt9.bundle", "CONIN$", "CONOUT$", "con.txt", "NUL.package",
    };
    for (const std::string_view name : windowsReservedNames) {
        Require(kb::editor::package_input::IsWindowsReservedDeviceName(name),
            "A reserved Windows device name was not recognized");
    }
    project.executableName = "con.txt";
    Require(!kb::editor::BuildGamePanelModel::Validate(WindowsX64, project, local, false, false).canBuild,
        "UI accepted a reserved Windows executable name");
    Require(kb::editor::BuildGamePanelModel::Validate(WebGpuWasm32, project, local, false, false).canBuild,
        "A Windows-only reserved device rule leaked into Web bundle validation");
    project.executableName = "Game";
    for (const kb::packaging::PackagingTargetSpec& target : kb::packaging::PackagingTargets()) {
        Require(kb::editor::BuildGamePanelModel::Validate(target.target, project, local, false, false).canBuild,
            "A configured target did not enable its Development BUILD action");
    }
    Require(!kb::editor::BuildGamePanelModel::Validate(WindowsX64, project, local, false, true).canBuild,
        "BUILD remained enabled during an active job");

    std::string pasted = "old";
    bool selectAll = true;
    Require(kb::editor::BuildGamePanelModel::InsertPrintableText(
        pasted, selectAll, "\nabc\x7f" "DEF", 5U), "Printable paste operation was rejected");
    Require(pasted == "abcDE" && !selectAll, "Paste did not filter ASCII or respect select-all and its limit");
    pasted = "1234";
    Require(kb::editor::BuildGamePanelModel::InsertPrintableText(pasted, selectAll, "XYZ", 5U) && pasted == "1234X",
        "Paste did not respect remaining field capacity");
}

void SettingsRoundTripTest() {
#if !defined(KB_EDITOR_BUILD_GAME_TEST_ROOT)
#error KB_EDITOR_BUILD_GAME_TEST_ROOT must contain this test's repository-local scratch directory
#endif
    const std::filesystem::path scratchBase =
        std::filesystem::path{ KB_EDITOR_BUILD_GAME_TEST_ROOT }.lexically_normal();
    const std::filesystem::path root = scratchBase /
        ("run-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::error_code error;
    std::filesystem::create_directories(root / "Config", error);
    Require(!error, "Test directory could not be created");
    kb::project::ProjectSettings project;
    project.name = "Project";
    project.gameName = "Product";
    project.publisher = "Publisher";
    project.version = "4.5.6";
    project.executableName = "ProductGame";
    project.applicationIcon = "Branding/ApplicationIcon.png";
    project.androidApplicationId = "com.publisher.product";
    project.androidVersionCode = 42U;
    project.androidLabel = "Product Android";
    std::string saveError;
    const auto projectPath = kb::project::ProjectSettingsStore::FilePath(root);
    Require(kb::project::ProjectSettingsStore::Save(projectPath, project, saveError), "Project packaging settings save failed");
    const auto loadedProject = kb::project::ProjectSettingsStore::Load(projectPath);
    Require(loadedProject.Succeeded() && loadedProject.settings == project, "Project packaging settings did not round-trip");

    const std::filesystem::path icon = root / "Branding" / "ApplicationIcon.png";
    std::filesystem::create_directories(icon.parent_path(), error);
    constexpr std::array<unsigned char, 8U> pngSignature{ 0x89U, 0x50U, 0x4eU, 0x47U, 0x0dU, 0x0aU, 0x1aU, 0x0aU };
    {
        std::ofstream stream{ icon, std::ios::binary };
        stream.write(reinterpret_cast<const char*>(pngSignature.data()), static_cast<std::streamsize>(pngSignature.size()));
    }
    Require(kb::editor::package_input::IsNormalProjectRelativePath("Branding/ApplicationIcon.png") &&
            !kb::editor::package_input::IsNormalProjectRelativePath("../ApplicationIcon.png"),
        "Project icon relative-path validation accepted traversal");
    Require(kb::editor::package_input::IsValidProjectPngIcon(root / "Project.21kbproject", icon),
        "Project PNG icon validation rejected an in-project PNG");

    std::array<unsigned char, 9U> previousIcon{};
    std::copy(pngSignature.begin(), pngSignature.end(), previousIcon.begin());
    previousIcon.back() = 0x11U;
    std::array<unsigned char, 9U> importedIcon{};
    std::copy(pngSignature.begin(), pngSignature.end(), importedIcon.begin());
    importedIcon.back() = 0x22U;
    const std::filesystem::path importedIconPath = root / "Imported.png";
    {
        std::ofstream stream{ icon, std::ios::binary | std::ios::trunc };
        stream.write(reinterpret_cast<const char*>(previousIcon.data()), static_cast<std::streamsize>(previousIcon.size()));
    }
    {
        std::ofstream stream{ importedIconPath, std::ios::binary | std::ios::trunc };
        stream.write(reinterpret_cast<const char*>(importedIcon.data()), static_cast<std::streamsize>(importedIcon.size()));
    }
    kb::editor::EditorProjectIconTransaction iconTransaction;
    std::string iconError;
    Require(iconTransaction.Publish(importedIconPath, root, iconError),
        "Application icon transaction could not publish its fixture");
    Require(iconTransaction.Rollback(iconError),
        "Application icon transaction could not roll back a failed settings save");
    std::array<unsigned char, previousIcon.size()> restoredIcon{};
    {
        std::ifstream stream{ icon, std::ios::binary };
        stream.read(reinterpret_cast<char*>(restoredIcon.data()), static_cast<std::streamsize>(restoredIcon.size()));
        Require(stream.gcount() == static_cast<std::streamsize>(restoredIcon.size()),
            "Application icon rollback restored a truncated file");
    }
    Require(restoredIcon == previousIcon,
        "Application icon rollback did not preserve the previous project asset bytes");

    const std::filesystem::path linkedProject = scratchBase /
        (root.filename().string() + "-linked-project");
    const std::filesystem::path outsideBranding = scratchBase /
        (root.filename().string() + "-outside-branding");
    std::filesystem::create_directories(linkedProject, error);
    Require(!error, "Symlink project fixture could not be created");
    std::filesystem::create_directories(outsideBranding, error);
    Require(!error, "Outside Branding fixture could not be created");
    const std::filesystem::path outsideSentinel = outsideBranding / "sentinel.bin";
    {
        std::ofstream stream{ outsideSentinel, std::ios::binary };
        stream << "outside-unchanged";
    }
    Require(CreateDirectoryLink(linkedProject / "Branding", outsideBranding, error),
        "Directory-symlink fixture could not be created");
    kb::editor::EditorProjectIconTransaction linkedIconTransaction;
    Require(!linkedIconTransaction.Publish(importedIconPath, linkedProject, iconError),
        "Application icon transaction followed a Branding directory symlink");
    Require(!std::filesystem::exists(outsideBranding / "ApplicationIcon.png"),
        "Rejected icon import changed a file outside the project");
    {
        std::ifstream stream{ outsideSentinel, std::ios::binary };
        const std::string bytes(
            std::istreambuf_iterator<char>{ stream }, std::istreambuf_iterator<char>{});
        Require(bytes == "outside-unchanged", "Rejected icon import changed an outside sentinel");
    }

    const std::filesystem::path outsideIcon = root.parent_path() / (root.filename().string() + "-outside.png");
    {
        std::ofstream stream{ outsideIcon, std::ios::binary };
        stream.write(reinterpret_cast<const char*>(pngSignature.data()), static_cast<std::streamsize>(pngSignature.size()));
    }
    Require(!kb::editor::package_input::IsValidProjectPngIcon(root / "Project.21kbproject", outsideIcon),
        "Project PNG icon validation accepted an outside file");
    Require(kb::editor::package_input::IsPathInsideOrEqual(root, root / "Build") &&
            !kb::editor::package_input::IsPathInsideOrEqual(root, root.parent_path() / "Build"),
        "Package work-root containment validation is inconsistent");

    kb::editor::EditorBuildGameSettings local;
    local.builderExecutable = "C:/Python/python.exe";
    local.buildRoot = "C:/Build";
    local.emsdkRoot = "C:/emsdk";
    local.linuxHost = "builder.example";
    local.linuxUser = "builder";
    local.linuxHostKey = "ssh-ed25519 AAAAC3NzaC1lZDI1NTE5AAAAITest";
    local.linuxPort = 2222U;
    local.linuxEngineRoot = "/opt/21kb";
    local.linuxDisplay = ":1";
    local.linuxIdentity = "C:/Keys/linux-builder";
    local.For(kb::packaging::PackagingTarget::AndroidAstcArm64).outputDirectory = "C:/Output";
    local.For(kb::packaging::PackagingTarget::AndroidAstcArm64).launchAfterBuild = true;
    local.For(kb::packaging::PackagingTarget::AndroidAstcArm64).androidKeystore = "C:/Keys/release.jks";
    local.For(kb::packaging::PackagingTarget::AndroidAstcArm64).androidKeyAlias = "release";
    const auto localPath = root / "Local" / "Packaging.ini";
    Require(kb::editor::EditorBuildGameSettingsStore::Save(localPath, local, saveError), "Local packaging settings save failed");
    const auto loadedLocal = kb::editor::EditorBuildGameSettingsStore::Load(localPath);
    Require(loadedLocal.Succeeded() && loadedLocal.settings == local, "Local packaging settings did not round-trip");
    const std::filesystem::path invalidPath = root / "Invalid" / "Packaging.ini";
    std::filesystem::create_directories(invalidPath.parent_path(), error);
    Require(!error, "Invalid-settings fixture directory could not be created");
    {
        std::ofstream invalid{ invalidPath, std::ios::binary | std::ios::trunc };
        invalid << "[Packaging.Global\n";
    }
    Require(!kb::editor::EditorBuildGameSettingsStore::Save(invalidPath, local, saveError) && !saveError.empty(),
        "Saving silently replaced malformed local packaging settings");
    {
        std::ifstream invalid{ invalidPath, std::ios::binary };
        const std::string bytes(
            std::istreambuf_iterator<char>{ invalid }, std::istreambuf_iterator<char>{});
        Require(bytes == "[Packaging.Global\n", "Rejected settings save modified the malformed source file");
    }
    bool invalidTargetRejected = false;
    try {
        static_cast<void>(local.For(static_cast<kb::packaging::PackagingTarget>(255)));
    } catch (const std::out_of_range&) {
        invalidTargetRejected = true;
    }
    Require(invalidTargetRejected, "Local settings silently mapped an unknown target to Windows");
    Require(kb::editor::EditorBuildGameSettingsStore::FilePath(root).lexically_normal().parent_path() != root.lexically_normal(),
        "Local packaging settings were placed inside the project");
}

void ProtocolAndArgumentsTest() {
    kb::editor::EditorPackageRequest request;
    request.packageScript = "C:/Engine/scripts/package_game.py";
    request.projectFile = "C:/Project/Project.21kbproject";
    request.targetId = "Android.ETC2.arm64";
    request.configuration = "Release";
    request.outputDirectory = "C:/Output/Game-Android.ETC2.arm64-Release";
    request.engineRoot = "C:/Engine";
    request.buildRoot = "C:/Build";
    request.productName = "Game";
    request.publisher = "Publisher";
    request.version = "1.0.0";
    request.executableName = "Game";
    request.androidApplicationId = "com.publisher.game";
    request.androidVersionCode = 2U;
    request.androidLabel = "Game Android";
    request.androidKeystore = "C:/Keys/release.jks";
    request.androidKeyAlias = "release";
    request.androidStorePassword = "secret-store";
    request.androidKeyPassword = "secret-key";
    const auto arguments = kb::editor::EditorProjectPackageService::BuildArguments(request);
    const auto has = [&](std::wstring_view value) { return std::ranges::find(arguments, value) != arguments.end(); };
    Require(has(L"Android.ETC2.arm64") && has(L"--android-label") && has(L"--android-keystore") && has(L"--android-key-alias"),
        "Android Release argv misses packaging metadata");
    Require(!has(L"secret-store") && !has(L"secret-key"), "Signing secret leaked into argv");
    Require(!has(L"--application-icon"), "Empty application icon emitted a dangling argv option");
    Require(kb::editor::EditorProjectPackageService::ResultMatchesRequest(request, request.outputDirectory),
        "Exact RESULT directory was rejected");
    Require(!kb::editor::EditorProjectPackageService::ResultMatchesRequest(request, "C:/Output/Other"),
        "Mismatched RESULT directory was accepted");
    const auto signing = kb::editor::EditorProjectPackageService::ParseProtocolLine(
        "SIGNING_REQUEST|C:/Job/request.json|C:/Job/response.json");
    Require(signing.kind == kb::editor::EditorPackageProtocolEvent::Kind::SigningRequest,
        "Signing request marker was not parsed");
    Require(kb::editor::EditorProjectPackageService::ParseProtocolLine(
        "SIGNING_REQUEST|relative.json|C:/Job/response.json").kind == kb::editor::EditorPackageProtocolEvent::Kind::None,
        "Relative signing request path was accepted");
    const auto stage = kb::editor::EditorProjectPackageService::ParseProtocolLine("STAGE|Verify|100|Verified");
    Require(stage.kind == kb::editor::EditorPackageProtocolEvent::Kind::Stage && stage.progress == 100,
        "Stage protocol did not parse");

    kb::editor::EditorPackageRequest web = request;
    web.targetId = "WebGL.wasm32";
    web.configuration = "Development";
    web.emsdkRoot = "C:/emsdk";
    const auto webArguments = kb::editor::EditorProjectPackageService::BuildArguments(web);
    Require(std::ranges::find(webArguments, L"--emsdk") != webArguments.end() &&
        std::ranges::find(webArguments, L"C:/emsdk") != webArguments.end(), "Web argv misses Emscripten SDK");
    Require(std::ranges::find(webArguments, L"--android-application-id") == webArguments.end(),
        "Web argv leaked Android options");

    kb::editor::EditorPackageRequest linux = request;
    linux.targetId = "Linux.x64";
    linux.configuration = "Development";
    linux.linuxHost = "builder.example";
    linux.linuxUser = "builder";
    linux.linuxHostKey = "ssh-ed25519 AAAAC3NzaC1lZDI1NTE5AAAAITest";
    linux.linuxPort = 2222U;
    linux.linuxEngineRoot = "/opt/21kb";
    linux.linuxDisplay = ":1";
    linux.linuxIdentity = "C:/Keys/linux-builder";
    const auto linuxArguments = kb::editor::EditorProjectPackageService::BuildArguments(linux);
    for (const std::wstring_view option : { L"--linux-host", L"--linux-user", L"--linux-host-key", L"--linux-port",
             L"--linux-engine-root", L"--linux-display", L"--linux-identity" }) {
        Require(std::ranges::find(linuxArguments, option) != linuxArguments.end(), "Linux argv misses an SSH option");
    }
}

#if defined(_WIN32)
void ProcessEnvironmentTest() {
    for (const std::wstring_view name : {
             L"ANDROID_KEYSTORE_PASSWORD", L"ANDROID_KEY_PASSWORD",
             L"KB_ANDROID_SIGNING_STORE_PASSWORD", L"KB_ANDROID_SIGNING_KEY_PASSWORD",
             L"ORG_GRADLE_PROJECT_kbSigningStorePassword",
             L"org_gradle_project_kbsigningkeypassword" }) {
        Require(kb::editor::package_process::IsBlockedEnvironmentVariable(name),
            "A package or ApkSigner password environment variable was not blocked");
    }
    Require(!kb::editor::package_process::IsBlockedEnvironmentVariable(L"ORG_GRADLE_PROJECT_org.gradle.jvmargs"),
        "A non-secret Gradle environment variable was blocked");

    const std::filesystem::path fakeJava =
        std::filesystem::path{ KB_EDITOR_BUILD_GAME_TEST_ROOT } / "untrusted-java" / "bin" / "java.exe";
    std::error_code error;
    std::filesystem::create_directories(fakeJava.parent_path(), error);
    Require(!error, "Untrusted Java fixture directory could not be created");
    {
        std::ofstream stream{ fakeJava, std::ios::binary | std::ios::trunc };
        stream << "not a trusted JVM";
    }
    Require(!kb::editor::EditorAndroidSigningBroker::IsTrustedJavaExecutable(fakeJava),
        "Android signing broker accepted a request-controlled Java executable");
}

void GeometryTest() {
    const RECT content{ 0, 0, 1280, 720 };
    const auto layout = kb::editor::BuildGamePanelLayout::Resolve(content);
    RECT previous{};
    for (int index = 0; index < 6; ++index) {
        const RECT row = kb::editor::BuildGamePanelLayout::TargetRow(layout.platformsList, index);
        Require(row.bottom <= layout.platformsList.bottom, "One of six target hit rows is clipped");
        if (index > 0) Require(row.top == previous.bottom, "Target hit rows overlap or leave a gap");
        previous = row;
    }
    Require(layout.buildButton.right > layout.buildButton.left, "BUILD button has no hit area");
}
#endif

} // namespace

int main() {
    try {
        TargetCatalogTest();
        SchemaAndValidationTest();
        SettingsRoundTripTest();
        ProtocolAndArgumentsTest();
#if defined(_WIN32)
        ProcessEnvironmentTest();
        GeometryTest();
#endif
        std::cout << "Editor build game tests passed.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
