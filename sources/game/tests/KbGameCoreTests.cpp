// The project bootstrap kb_game and kb_standalone_player share. It was lifted
// out of the sample as 239 lines of working code, and the only check it had -
// launching the real executable against a project with no plugins, no physics
// layers and no input mapping - never reached most of it: deleting the plugin
// path rewrite, the required-module check, the legacy-settings fallback, the
// input activation and the mapping context left every test in the repository
// green. These run the bootstrap directly, on projects that have those things.

#include "GameProjectRuntime.hpp"
#include "PackagedRuntimeModuleContract.hpp"
#include "ProjectCooker.hpp"

#include "engine/assets/AssetManager.hpp"
#include "engine/assets/AssetMetadata.hpp"
#include "engine/assets/AssetRegistry.hpp"
#include "engine/assets/bake/BakeTargetProfile.hpp"
#include "engine/assets/bake/RuntimeAssetPack.hpp"
#include "engine/input/InputAssetIO.hpp"
#include "engine/input/InputMappingContextAsset.hpp"
#include "engine/input/InputSubsystem.hpp"
#include "engine/project/ProjectManager.hpp"
#include "engine/project/ProjectSettings.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneAssets.hpp"
#include "engine/scene/SceneComponents.hpp"
#include "engine/scene/SceneDocumentService.hpp"
#include "engine/scene/SceneEntities.hpp"
#include "engine/scene/SceneObject.hpp"
#include "engine/scene/SceneObjectDesc.hpp"
#include "engine/scene/MeshRendererComponent.hpp"
#include "kb/render/RuntimeAssetShaderProvider.hpp"
#include "kb/render/resources/RenderMaterialAssetLoader.hpp"
#include "kb/render/resources/RenderMaterialAssetWriter.hpp"
#include "kb/render/resources/RenderMaterialGraphAssetLoader.hpp"
#include "kb/render/resources/RenderMaterialGraphDocument.hpp"
#include "kb/render/resources/RenderMaterialGraphShaderArtifact.hpp"
#include "kb/render/resources/RenderMaterialParameterCollection.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#ifndef KB_WINDOWS_RUNTIME_MODULE_TEST_PLUGIN_PATH
#error KB_WINDOWS_RUNTIME_MODULE_TEST_PLUGIN_PATH must name the real package-module test DLL
#endif

#ifndef KB_WINDOWS_PHYSICS_PLUGIN_PATH
#error KB_WINDOWS_PHYSICS_PLUGIN_PATH must name the real Windows physics provider DLL
#endif

#ifndef KB_GAME_CORE_TEST_ROOT
#error KB_GAME_CORE_TEST_ROOT must name repository-local test scratch
#endif

namespace {

using Clock = std::chrono::steady_clock;

void Require(bool condition, const char* message) {
    if (!condition) {
        std::fputs(message, stderr);
        std::fputs("\n", stderr);
        std::exit(EXIT_FAILURE);
    }
}

[[nodiscard]] std::filesystem::path TestRoot() {
    static const std::filesystem::path root =
        std::filesystem::path{ KB_GAME_CORE_TEST_ROOT } /
        ("run-" + std::to_string(GetCurrentProcessId()) + "-" +
            std::to_string(Clock::now().time_since_epoch().count()));
    return root;
}

void WriteTextFile(const std::filesystem::path& path, const std::string& text) {
    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);
    Require(!error, "kb_game_core test directory could not be created");
    std::ofstream output{ path, std::ios::binary | std::ios::trunc };
    Require(output.is_open(), "kb_game_core test file could not be opened");
    output << text;
    Require(output.good(), "kb_game_core test file could not be written");
}

[[nodiscard]] bool Mentions(const std::string& text, const std::string& needle) {
    return text.find(needle) != std::string::npos;
}

// -------------------------------------------------------------------------
// The time step the loop hands the scene runtime.
// -------------------------------------------------------------------------

void RunRuntimeDeltaTests() {
    const Clock::time_point base = Clock::now();

    Require(
        kb::game::RuntimeDeltaSeconds(base, base) == 0.0F,
        "A frame that took no time must step the scene by no time");
    Require(
        kb::game::RuntimeDeltaSeconds(base + std::chrono::seconds{ 1 }, base) == 0.0F,
        "A clock that appears to go backwards must never step the scene by a negative amount");

    // Between zero and the ceiling the measurement is handed through unchanged;
    // a step that is always the ceiling would satisfy the ceiling rule alone.
    const float small =
        kb::game::RuntimeDeltaSeconds(base, base + std::chrono::milliseconds{ 5 });
    Require(
        std::fabs(small - 0.005F) < 1.0e-4F,
        "A frame well under the ceiling must be stepped by the time it actually took");
    Require(
        small < kb::game::kMaximumRuntimeDeltaSeconds,
        "A five millisecond frame is not the ceiling");

    // A stall must not tunnel a moving body through the world, and no stall may
    // be worse than any other: an hour and a second have to arrive as the same
    // step, or the ceiling is not a ceiling.
    const float second =
        kb::game::RuntimeDeltaSeconds(base, base + std::chrono::seconds{ 1 });
    const float hour = kb::game::RuntimeDeltaSeconds(base, base + std::chrono::hours{ 1 });
    Require(
        second == kb::game::kMaximumRuntimeDeltaSeconds,
        "A one second stall must be stepped as the ceiling");
    Require(second == hour, "Every stall past the ceiling must produce the same step");
    Require(
        kb::game::kMaximumRuntimeDeltaSeconds > 0.0F &&
            kb::game::kMaximumRuntimeDeltaSeconds <= 1.0F / 15.0F,
        "The step ceiling must be positive and no looser than a fifteenth of a second");
    Require(
        kb::game::RuntimeDeltaSeconds(
            base, base + std::chrono::duration_cast<Clock::duration>(
                             std::chrono::duration<float>{ kb::game::kMaximumRuntimeDeltaSeconds })) <=
            kb::game::kMaximumRuntimeDeltaSeconds,
        "A frame exactly at the ceiling must not step past it");

    Clock::time_point pausedOrigin = base;
    const Clock::time_point resumed = base + std::chrono::hours{ 1 };
    kb::game::ResetRuntimeDeltaOrigin(pausedOrigin, resumed);
    const float resumedDelta = kb::game::RuntimeDeltaSeconds(
        pausedOrigin, resumed + std::chrono::milliseconds{ 5 });
    Require(
        std::fabs(resumedDelta - 0.005F) < 1.0e-4F,
        "A resumed host included paused wall time in its first simulation step");
}

void RunPackagedRuntimeModuleContractTests() {
    Require(
        kb::game::IsSafeWindowsRuntimeModuleRelativePath("RuntimeModules/Company/custom.dll") &&
            !kb::game::IsSafeWindowsRuntimeModuleRelativePath("../outside.dll") &&
            !kb::game::IsSafeWindowsRuntimeModuleRelativePath("C:/outside.dll") &&
            !kb::game::IsSafeWindowsRuntimeModuleRelativePath("RuntimeModules/NUL.dll") &&
            !kb::game::IsSafeWindowsRuntimeModuleRelativePath("COM1/custom.dll"),
        "Windows cooker and mounted runtime do not share a strict relative-DLL path contract");
    kb::project::ProjectDescriptor supported{};
    for (const kb::game::PackagedRuntimeModuleDesc& module : kb::game::kPackagedRuntimeModules) {
        supported.plugins.push_back(kb::project::ProjectPluginReference{
            .name = std::string{ module.name },
            .binaryPath = "desktop-name-is-irrelevant.dll",
            .enabled = true,
        });
    }
    constexpr std::array<std::string_view, 5U> kMonolithicTargets{
        "Android.ASTC.arm64",
        "Android.ETC2.arm64",
        "Linux.x64",
        "WebGL.wasm32",
        "WebGPU.wasm32",
    };
    for (const std::string_view target : kMonolithicTargets) {
        Require(
            !kb::game::FirstUnsupportedPackagedRuntimeModule(target, supported).has_value(),
            "A monolithic host rejected a provider compiled into its package");
    }

    kb::project::ProjectDescriptor unsupported = supported;
    unsupported.plugins.push_back(kb::project::ProjectPluginReference{
        .name = "Company.CustomDesktopPlugin",
        .binaryPath = "custom_plugin.dll",
        .enabled = true,
    });
    const std::optional<std::string_view> rejected =
        kb::game::FirstUnsupportedPackagedRuntimeModule("Android.ASTC.arm64", unsupported);
    Require(rejected.has_value() && *rejected == "Company.CustomDesktopPlugin",
        "Android accepted a descriptor module its static host cannot construct");

    unsupported.plugins.back().enabled = false;
    Require(
        !kb::game::FirstUnsupportedPackagedRuntimeModule("Android.ETC2.arm64", unsupported).has_value(),
        "Android rejected a disabled module that is not part of the shipped runtime");
    unsupported.plugins.back().enabled = true;
    Require(
        !kb::game::FirstUnsupportedPackagedRuntimeModule("Windows.x64", unsupported).has_value(),
        "The dynamic Windows host was accidentally restricted to Android's static providers");

    const std::filesystem::path projectRoot = TestRoot() / "unsupported_android_module";
    std::error_code error;
    std::filesystem::create_directories(projectRoot, error);
    Require(!error, "Android module rejection fixture could not be created");
    Require(
        kb::project::ProjectManager::SaveProject(
            projectRoot / "Project.21kbproject", unsupported),
        "Android module rejection descriptor could not be written");

    const std::filesystem::path outputPack = projectRoot / "must_not_exist.kbpack";
    std::ostringstream diagnostics;
    const kb::game::ProjectCookResult cook = kb::game::CookProject(
        kb::game::ProjectCookRequest{
            .projectPath = projectRoot,
            .targetProfileId = "Android.ASTC.arm64",
            .outputPackPath = outputPack,
        },
        diagnostics);
    Require(!cook.succeeded, "Android cooker accepted a module absent from its static host");
    Require(
        Mentions(cook.error, "Company.CustomDesktopPlugin"),
        "Android cooker rejection did not identify the unsupported module");
    Require(
        !std::filesystem::exists(outputPack),
        "Android cooker wrote a package after rejecting an unsupported module");
}

// -------------------------------------------------------------------------
// Narrowing a name this machine's code page cannot spell.
// -------------------------------------------------------------------------

void RunNarrowingTests() {
    static constexpr wchar_t unspellableLetters[] = { 0x65E5, 0x672C, 0x8A9E, 0x0416, 0x03A9, 0 };
    const std::wstring unspellable = std::wstring{ L"kb_game_" } + unspellableLetters;

    Require(
        kb::game::TryNarrow(L"").value_or(std::string{ "missing" }).empty(),
        "An empty name narrows to an empty string rather than failing");
    Require(
        kb::game::TryNarrow(L"C:/Games/Ordinary Path/Project.21kbproject").value_or(
            std::string{}) == "C:/Games/Ordinary Path/Project.21kbproject",
        "A name the code page can spell must narrow to itself");
    Require(
        kb::game::NarrowForDiagnostics(std::wstring_view{ L"plain" }) == "plain",
        "A diagnostic must not disturb a name the code page can spell");

    // The point of the pair: an exact answer is refused rather than invented, and
    // a printable answer always exists, because a diagnostic that throws in a
    // windowed process becomes abort() behind a dialog nobody can dismiss.
    if (GetACP() != CP_UTF8) {
        Require(
            !kb::game::TryNarrow(unspellable).has_value(),
            "A name the code page cannot spell must be refused, not silently mangled");
    }
    const std::string printable = kb::game::NarrowForDiagnostics(std::wstring_view{ unspellable });
    Require(!printable.empty(), "A name that cannot be spelled must still be printable");
    Require(
        Mentions(printable, "kb_game_"),
        "A printable diagnostic must keep the part of the name that can be spelled");
}

// -------------------------------------------------------------------------
// A project the bootstrap has to read.
// -------------------------------------------------------------------------

struct Fixture {
    std::filesystem::path root;
    std::string sceneVirtualPath = "/Game/Scenes/Main.21kbscene";
    std::string behaviourVirtualPath = "/Game/Logic/Player.lua";
};

[[nodiscard]] Fixture BuildFixture(const std::filesystem::path& root, const std::string& projectName) {
    std::error_code error;
    std::filesystem::create_directories(root, error);
    Require(!error, "kb_game_core test project directory could not be prepared");

    WriteTextFile(root / "Assets" / "Logic" / "Player.lua", "function Tick(self, dt) end\n");

    kb::scene::Scene authored;
    Require(
        authored.Assets().MountProject(root),
        "kb_game_core test project could not be mounted for authoring");
    static_cast<void>(authored.Assets().Discover());
    for (std::size_t index = 0U; index < 3U; ++index) {
        static_cast<void>(authored.Entities().CreateObject(kb::scene::SceneObjectDesc{
            .name = "Main_" + std::to_string(index),
        }));
    }
    Require(
        kb::scene::SceneDocumentService::Save(
            authored, root / "Assets" / "Scenes" / "Main.21kbscene", "Main"),
        "kb_game_core test scene could not be saved");

    const kb::project::ProjectDescriptor descriptor;
    Require(
        kb::project::ProjectManager::SaveProject(root / (projectName + ".21kbproject"), descriptor),
        "kb_game_core test project descriptor could not be written");
    return Fixture{ .root = root };
}

void WriteSettings(const std::filesystem::path& root, const kb::project::ProjectSettings& settings) {
    std::string error;
    Require(
        kb::project::ProjectSettingsStore::Save(
            kb::project::ProjectSettingsStore::FilePath(root), settings, error),
        "kb_game_core test project settings could not be written");
}

class ScopedExternalCookOutputLock final {
public:
    explicit ScopedExternalCookOutputLock(const std::filesystem::path& outputPath) {
        lockPath_ = std::filesystem::path{ outputPath.native() + std::wstring{ L".kbpacklock" } };
        handle_ = CreateFileW(
            lockPath_.c_str(),
            GENERIC_READ | GENERIC_WRITE,
            0,
            nullptr,
            OPEN_ALWAYS,
            FILE_ATTRIBUTE_NORMAL,
            nullptr);
        Require(handle_ != INVALID_HANDLE_VALUE,
            "External cook-output lock fixture could not acquire its lock");
    }
    ScopedExternalCookOutputLock(const ScopedExternalCookOutputLock&) = delete;
    ScopedExternalCookOutputLock& operator=(const ScopedExternalCookOutputLock&) = delete;
    ~ScopedExternalCookOutputLock() {
        if (handle_ != INVALID_HANDLE_VALUE) {
            static_cast<void>(CloseHandle(handle_));
            std::error_code removeError;
            std::filesystem::remove(lockPath_, removeError);
        }
    }

private:
    std::filesystem::path lockPath_;
    HANDLE handle_ = INVALID_HANDLE_VALUE;
};

void RunCookOutputLockTest() {
    const Fixture fixture = BuildFixture(TestRoot() / "cook_output_lock", "Project");
    kb::project::ProjectSettings settings;
    settings.defaultMap = fixture.sceneVirtualPath;
    settings.physicsLayersAsset.clear();
    settings.inputEnabled = false;
    WriteSettings(fixture.root, settings);

    const std::filesystem::path outputPack = fixture.root / "Game.kbpack";
    WriteTextFile(outputPack, "previous published package");
    {
        const ScopedExternalCookOutputLock lock{ outputPack };
        std::ostringstream diagnostics;
        const kb::game::ProjectCookResult cook = kb::game::CookProject(
            kb::game::ProjectCookRequest{
                .projectPath = fixture.root,
                .targetProfileId = "Windows.x64",
                .outputPackPath = outputPack,
            },
            diagnostics);
        const std::string lockFailure =
            "CookProject did not fail closed when its exact output was locked: " + cook.error;
        Require(!cook.succeeded && Mentions(cook.error, "already being cooked"),
            lockFailure.c_str());

        std::ifstream published{ outputPack, std::ios::binary };
        const std::string bytes{
            std::istreambuf_iterator<char>{ published }, std::istreambuf_iterator<char>{} };
        Require(bytes == "previous published package",
            "A cook without the output lock replaced the previously published package");
        const bool leftCandidate = std::ranges::any_of(
            std::filesystem::directory_iterator(fixture.root),
            [](const std::filesystem::directory_entry& entry) {
                const std::string name = entry.path().filename().generic_string();
                return name.starts_with(".kb-cook-") && name != ".kb-cook-cache";
            });
        Require(!leftCandidate,
            "CookProject created a candidate before acquiring the final-output lock");
    }
}

void RunWindowsRuntimeModulePackagingTests() {
    namespace bake = kb::assets::bake;

    const Fixture fixture = BuildFixture(TestRoot() / "windows_custom_plugin_project", "Project");
    kb::project::ProjectSettings settings;
    settings.defaultMap = fixture.sceneVirtualPath;
    settings.physicsLayersAsset.clear();
    settings.inputEnabled = false;
    WriteSettings(fixture.root, settings);
    std::error_code copyError;
    std::filesystem::create_directories(fixture.root / "Binaries", copyError);
    Require(!copyError && std::filesystem::copy_file(
                std::filesystem::path{ KB_WINDOWS_RUNTIME_MODULE_TEST_PLUGIN_PATH },
                fixture.root / "Binaries" / "custom.dll",
                std::filesystem::copy_options::overwrite_existing,
                copyError) && !copyError,
        "Real Windows runtime-module test DLL could not be copied into the project snapshot");

    kb::project::ProjectDescriptor descriptor;
    descriptor.plugins = {
        kb::project::ProjectPluginReference{
            .name = "Tests.PackagedWindowsRuntime",
            .binaryPath = "Binaries/custom.dll",
            .enabled = true,
        },
        kb::project::ProjectPluginReference{
            .name = "Physics.Jolt",
            .binaryPath = "authoring-path-must-not-ship.dll",
            .enabled = true,
        },
    };
    Require(kb::project::ProjectManager::SaveProject(
                fixture.root / "Project.21kbproject", descriptor),
        "Windows runtime-module fixture descriptor could not be written");

    const std::filesystem::path sealedRoot = TestRoot() / "windows_custom_plugin_package";
    std::error_code directoryError;
    std::filesystem::create_directories(sealedRoot, directoryError);
    Require(!directoryError, "Windows runtime-module package root could not be created");
    const std::filesystem::path packPath = sealedRoot / "Game.kbpack";
    const std::filesystem::path stagedModules = sealedRoot / "RuntimeModules";
    std::ostringstream diagnostics;
    const kb::game::ProjectCookResult cook = kb::game::CookProject(
        kb::game::ProjectCookRequest{
            .projectPath = fixture.root,
            .targetProfileId = "Windows.x64",
            .outputPackPath = packPath,
            .runtimeModulesOutputDirectory = stagedModules,
        },
        diagnostics);
    Require(cook.succeeded, cook.error.c_str());
    Require(
        std::filesystem::is_regular_file(stagedModules / "Binaries" / "custom.dll"),
        "Windows cooker did not stage the custom DLL under RuntimeModules");
    copyError.clear();
    Require(std::filesystem::copy_file(
                std::filesystem::path{ KB_WINDOWS_PHYSICS_PLUGIN_PATH },
                sealedRoot / "kb_physics_jolt_plugin.dll",
                std::filesystem::copy_options::overwrite_existing,
                copyError) && !copyError,
        "Real Windows physics provider DLL could not be copied into the sealed package root");

    auto pack = std::make_shared<bake::RuntimeAssetPack>();
    Require(pack->Mount(packPath, bake::WindowsX64BakeTargetProfile()) ==
            bake::RuntimeAssetPackStatus::Success,
        "Windows runtime-module package did not mount");
    Require(pack->Manifest().descriptor.plugins.size() == 2U &&
            pack->Manifest().descriptor.plugins[0].binaryPath ==
                "RuntimeModules/Binaries/custom.dll" &&
            pack->Manifest().descriptor.plugins[1].binaryPath ==
                "kb_physics_jolt_plugin.dll",
        "Cooked manifest did not contain sealed custom and canonical built-in DLL paths");

    std::ostringstream runtimeError;
    kb::game::GameProjectRuntime runtime{};
    Require(kb::game::ReadMountedGameProjectRuntime(
                pack, sealedRoot, "", runtime, runtimeError),
        "Mounted Windows runtime did not accept its sealed module DLLs");
    Require(
        std::filesystem::path{ runtime.descriptor.plugins[0].binaryPath } ==
            std::filesystem::weakly_canonical(stagedModules / "Binaries" / "custom.dll") &&
        std::filesystem::path{ runtime.descriptor.plugins[1].binaryPath } ==
            std::filesystem::weakly_canonical(sealedRoot / "kb_physics_jolt_plugin.dll"),
        "Mounted Windows runtime did not rebase module paths to the sealed package root");
    {
        kb::scene::Scene moduleScene{ runtime.descriptor };
        Require(moduleScene.ModuleDiagnostics().empty() &&
                moduleScene.IsModuleActive("Tests.PackagedWindowsRuntime") &&
                moduleScene.IsModuleActive("Physics.Jolt"),
            "Sealed Windows runtime DLLs did not pass LoadLibrary, ABI and module activation");
    }

    const std::filesystem::path emptyRoot = TestRoot() / "windows_missing_plugin_package";
    directoryError.clear();
    std::filesystem::create_directories(emptyRoot, directoryError);
    Require(!directoryError, "Missing-module runtime root could not be created");
    std::ostringstream missingError;
    kb::game::GameProjectRuntime missingRuntime{};
    Require(!kb::game::ReadMountedGameProjectRuntime(
                pack, emptyRoot, "", missingRuntime, missingError) &&
            Mentions(missingError.str(), "missing"),
        "Mounted Windows runtime accepted a package root without its declared DLLs");
    pack->Unmount();

    const Fixture absoluteFixture =
        BuildFixture(TestRoot() / "windows_absolute_plugin_project", "Project");
    WriteSettings(absoluteFixture.root, settings);
    const std::filesystem::path absoluteDll =
        std::filesystem::absolute(absoluteFixture.root / "Binaries" / "absolute.dll");
    WriteTextFile(absoluteDll, "MZabsolute runtime module");
    kb::project::ProjectDescriptor absoluteDescriptor;
    absoluteDescriptor.plugins.push_back(kb::project::ProjectPluginReference{
        .name = "Company.AbsoluteRuntime",
        .binaryPath = absoluteDll.string(),
        .enabled = true,
    });
    Require(kb::project::ProjectManager::SaveProject(
                absoluteFixture.root / "Project.21kbproject", absoluteDescriptor),
        "Absolute runtime-module fixture descriptor could not be written");
    const std::filesystem::path rejectedPack =
        TestRoot() / "windows_absolute_plugin_package" / "Game.kbpack";
    const std::filesystem::path rejectedModules =
        TestRoot() / "windows_absolute_plugin_modules";
    std::ostringstream rejectedDiagnostics;
    const kb::game::ProjectCookResult rejected = kb::game::CookProject(
        kb::game::ProjectCookRequest{
            .projectPath = absoluteFixture.root,
            .targetProfileId = "Windows.x64",
            .outputPackPath = rejectedPack,
            .runtimeModulesOutputDirectory = rejectedModules,
        },
        rejectedDiagnostics);
    Require(!rejected.succeeded && Mentions(rejected.error, "safe project-relative DLL") &&
            !std::filesystem::exists(rejectedPack) &&
            !std::filesystem::exists(rejectedModules),
        "Windows cooker accepted or staged an absolute custom module DLL path");

    absoluteDescriptor.plugins[0].binaryPath = "../outside.dll";
    WriteTextFile(absoluteFixture.root.parent_path() / "outside.dll", "outside project");
    Require(kb::project::ProjectManager::SaveProject(
                absoluteFixture.root / "Project.21kbproject", absoluteDescriptor),
        "Traversal runtime-module fixture descriptor could not be written");
    std::ostringstream traversalDiagnostics;
    const kb::game::ProjectCookResult traversal = kb::game::CookProject(
        kb::game::ProjectCookRequest{
            .projectPath = absoluteFixture.root,
            .targetProfileId = "Windows.x64",
            .outputPackPath = TestRoot() / "windows_traversal_plugin_package" / "Game.kbpack",
            .runtimeModulesOutputDirectory =
                TestRoot() / "windows_traversal_plugin_modules",
        },
        traversalDiagnostics);
    Require(!traversal.succeeded && Mentions(traversal.error, "safe project-relative DLL"),
        "Windows cooker accepted a traversing custom module DLL path");

    absoluteDescriptor.plugins[0].binaryPath = "Binaries/missing.dll";
    Require(kb::project::ProjectManager::SaveProject(
                absoluteFixture.root / "Project.21kbproject", absoluteDescriptor),
        "Missing runtime-module fixture descriptor could not be written");
    std::ostringstream missingCookDiagnostics;
    const kb::game::ProjectCookResult missingCook = kb::game::CookProject(
        kb::game::ProjectCookRequest{
            .projectPath = absoluteFixture.root,
            .targetProfileId = "Windows.x64",
            .outputPackPath = TestRoot() / "windows_missing_plugin_cook" / "Game.kbpack",
            .runtimeModulesOutputDirectory = TestRoot() / "windows_missing_plugin_modules",
        },
        missingCookDiagnostics);
    Require(!missingCook.succeeded && Mentions(missingCook.error, "missing"),
        "Windows cooker accepted a missing custom module DLL");
}

void RunSceneMetaCookValidationTests() {
    const auto requireRejectedCook = [](
        const Fixture& fixture,
        std::string_view validationDiagnostic,
        std::string_view readerDiagnostic) {
        kb::project::ProjectSettings settings;
        settings.defaultMap = fixture.sceneVirtualPath;
        settings.physicsLayersAsset.clear();
        settings.inputEnabled = false;
        WriteSettings(fixture.root, settings);

        const std::filesystem::path outputPack = fixture.root / "must_not_exist.kbpack";
        std::ostringstream diagnostics;
        const kb::game::ProjectCookResult cook = kb::game::CookProject(
            kb::game::ProjectCookRequest{
                .projectPath = fixture.root,
                .targetProfileId = "Windows.x64",
                .outputPackPath = outputPack,
            },
            diagnostics);
        Require(!cook.succeeded, "Cooker accepted a scene without readable metadata");
        Require(
            Mentions(cook.error, fixture.sceneVirtualPath) &&
                Mentions(cook.error, std::string{ validationDiagnostic }),
            "Cooker rejection did not identify the scene metadata failure");
        Require(
            Mentions(cook.error, std::string{ readerDiagnostic }),
            "Cooker rejection did not preserve the metadata reader diagnostic");
        Require(
            !std::filesystem::exists(outputPack),
            "Cooker published a package after rejecting unreadable scene metadata");
    };

    {
        const Fixture fixture = BuildFixture(TestRoot() / "cook_missing_scene_meta", "Project");
        const std::filesystem::path metaPath =
            fixture.root / "Assets" / "Scenes" / "Main.meta";
        std::error_code error;
        Require(std::filesystem::remove(metaPath, error) && !error,
            "Missing-meta cook fixture could not remove its scene metadata");
        requireRejectedCook(fixture, "no readable scene metadata sidecar", "could not be opened");
    }

    {
        const Fixture fixture = BuildFixture(TestRoot() / "cook_corrupt_scene_meta", "Project");
        WriteTextFile(
            fixture.root / "Assets" / "Scenes" / "Main.meta",
            "not a 21kb scene metadata descriptor");
        requireRejectedCook(fixture, "no readable scene metadata sidecar", "descriptor fields are invalid");
    }

    {
        const Fixture fixture = BuildFixture(TestRoot() / "cook_stale_scene_meta", "Project");
        const std::filesystem::path scenePath =
            fixture.root / "Assets" / "Scenes" / "Main.21kbscene";
        const std::filesystem::path metaPath =
            fixture.root / "Assets" / "Scenes" / "Main.meta";
        const std::filesystem::path oldMetaPath = fixture.root / "Main.original.meta";
        std::error_code copyError;
        Require(std::filesystem::copy_file(metaPath, oldMetaPath, copyError) && !copyError,
            "Stale-meta cook fixture could not preserve the original metadata");

        kb::scene::Scene updated;
        for (std::size_t index = 0U; index < 4U; ++index) {
            static_cast<void>(updated.Entities().CreateObject(kb::scene::SceneObjectDesc{
                .name = "Updated_" + std::to_string(index),
            }));
        }
        Require(kb::scene::SceneDocumentService::Save(updated, scenePath, "Main"),
            "Stale-meta cook fixture could not write the updated scene");
        copyError.clear();
        Require(std::filesystem::copy_file(
                oldMetaPath, metaPath, std::filesystem::copy_options::overwrite_existing, copyError) && !copyError,
            "Stale-meta cook fixture could not restore the original metadata");
        requireRejectedCook(fixture, "does not match scene metadata sidecar", "integrity does not match");
    }
}

void RunAuthoritativeMaterialGraphCookTest() {
    namespace bake = kb::assets::bake;

    Require(kb::render::kRenderMaterialGraphShaderWrapperVersion == 9ULL,
        "Material graph shader cache version was not advanced for WGSL artifacts");
    Require(kb::render::RenderMaterialGraphShaderBackendName(
                kb::render::RenderMaterialGraphShaderBackend::Wgsl) == "wgsl" &&
            kb::render::RenderMaterialGraphShaderBackendProfile(
                kb::render::RenderMaterialGraphShaderBackend::Wgsl) == "wgsl" &&
            kb::render::ParseRenderMaterialGraphShaderBackend("wgsl") ==
                kb::render::RenderMaterialGraphShaderBackend::Wgsl,
        "Material graph WGSL backend identity or shaderc profile is incomplete");

    const auto makeColorOutputLink = [] {
        kb::render::RenderMaterialGraphLink link{
            .fromNodeId = 2U,
            .fromPinId = kb::render::RenderMaterialGraphStablePinId(
                kb::render::RenderMaterialGraphNodeKind::ConstantColor, "rgba", true),
            .fromPin = "rgba",
            .toNodeId = 1U,
            .toPinId = kb::render::RenderMaterialGraphStablePinId(
                kb::render::RenderMaterialGraphNodeKind::MaterialOutput, "baseColor", false),
            .toPin = "baseColor",
        };
        link.id = kb::render::MakeRenderMaterialGraphLinkId(link);
        return link;
    };
    const auto authorMaterialScene = [](const Fixture& fixture, kb::assets::AssetId materialId) {
        kb::scene::Scene scene;
        const kb::scene::SceneObject object = scene.Entities().CreateObject(
            kb::scene::SceneObjectDesc{ .name = "GraphMaterial" });
        scene.Components().MeshRenderers().Set(
            object.Entity(), kb::scene::MeshRendererComponent{ .materialAssetId = materialId.value });
        Require(kb::scene::SceneDocumentService::Save(
                    scene, fixture.root / "Assets" / "Scenes" / "Main.21kbscene", "Main"),
            "Graph-material cook fixture could not write its scene dependency");
    };
    const auto authorMaterial = [](const Fixture& fixture,
                                   const kb::render::RenderMaterialGraphDocument& inlineGraph,
                                   std::string graphPath,
                                   kb::assets::AssetId graphId) {
        kb::render::RenderMaterialAssetData material{};
        material.graph = inlineGraph;
        material.graphSourceAssetId = graphId.value;
        material.graphSourceAssetPath = std::move(graphPath);
        const std::filesystem::path materialPath =
            fixture.root / "Assets" / "Materials" / "External.kbmat";
        std::error_code directoryError;
        std::filesystem::create_directories(materialPath.parent_path(), directoryError);
        Require(!directoryError && kb::render::RenderMaterialAssetWriter::Save(materialPath, material),
            "Graph-material cook fixture could not write its material");
    };

    const std::string materialVirtualPath = "/Game/Materials/External.kbmat";
    const std::string graphVirtualPath = "/Game/Materials/Authoritative.kbmaterialgraph";
    const kb::assets::AssetId materialId =
        kb::assets::MakeAssetId(materialVirtualPath + ":RenderMaterial");

    kb::render::RenderMaterialGraphDocument inlineGraph =
        kb::render::MakeDefaultRenderMaterialGraphDocument();
    kb::render::RenderMaterialGraphDocument authoritativeGraph =
        kb::render::MakeDefaultRenderMaterialGraphDocument();
    inlineGraph.nodes.push_back(kb::render::RenderMaterialGraphNode{
        .id = 2U,
        .kind = kb::render::RenderMaterialGraphNodeKind::ConstantColor,
        .parameter = kb::render::RenderMaterialGraphParameterMetadata{
            .defaultValueHint = "1 0 0 1" },
    });
    inlineGraph.links.push_back(makeColorOutputLink());
    authoritativeGraph.storageModel = "material-graph-asset";
    authoritativeGraph.nodes.push_back(kb::render::RenderMaterialGraphNode{
        .id = 2U,
        .kind = kb::render::RenderMaterialGraphNodeKind::ConstantColor,
        .parameter = kb::render::RenderMaterialGraphParameterMetadata{
            .defaultValueHint = "0 0 1 1" },
    });
    authoritativeGraph.links.push_back(makeColorOutputLink());
    const kb::render::RenderMaterialGraphCompileResult inlineCompile =
        kb::render::CompileRenderMaterialGraphToShaderSource(inlineGraph);
    const kb::render::RenderMaterialGraphCompileResult authoritativeCompile =
        kb::render::CompileRenderMaterialGraphToShaderSource(authoritativeGraph);
    Require(inlineCompile.Succeeded() && authoritativeCompile.Succeeded() &&
            inlineCompile.shader.sourceHash != authoritativeCompile.shader.sourceHash,
        "Graph-material fixture did not produce distinct valid inline A and sourceGraph B shaders");
    const std::uint64_t authoritativeVariant =
        kb::render::ComputeRenderMaterialGraphVariantKey(authoritativeCompile.shader);

    const Fixture fixture = BuildFixture(TestRoot() / "authoritative_graph_cook", "Project");
    const std::filesystem::path graphPath =
        fixture.root / "Assets" / "Materials" / "Authoritative.kbmaterialgraph";
    std::error_code directoryError;
    std::filesystem::create_directories(graphPath.parent_path(), directoryError);
    Require(!directoryError &&
            kb::render::RenderMaterialGraphAssetLoader::SaveGraph(graphPath, authoritativeGraph),
        "Graph-material cook fixture could not write sourceGraph B");
    authorMaterial(fixture, inlineGraph, graphVirtualPath, {});
    authorMaterialScene(fixture, materialId);
    kb::project::ProjectSettings settings;
    settings.defaultMap = fixture.sceneVirtualPath;
    settings.physicsLayersAsset.clear();
    settings.inputEnabled = false;
    WriteSettings(fixture.root, settings);

    const std::filesystem::path outputPack = fixture.root / "Game.kbpack";
    std::ostringstream diagnostics;
    const kb::game::ProjectCookResult cook = kb::game::CookProject(
        kb::game::ProjectCookRequest{
            .projectPath = fixture.root,
            .targetProfileId = "WebGL.wasm32",
            .outputPackPath = outputPack,
        },
        diagnostics);
    Require(cook.succeeded, cook.error.c_str());
    Require(cook.shaderArtifactCount != 0U && std::filesystem::is_regular_file(outputPack),
        "CookProject failed to publish the authoritative sourceGraph package");
    const bool leftCookCandidate = std::ranges::any_of(
        std::filesystem::directory_iterator(fixture.root),
        [](const std::filesystem::directory_entry& entry) {
            const std::string name = entry.path().filename().generic_string();
            return name.starts_with(".kb-cook-") && name != ".kb-cook-cache";
        });
    Require(!leftCookCandidate,
        "CookProject left its validated package candidate or lock beside the published package");

    const bake::BakeTargetProfile profile = bake::WebGlWasm32BakeTargetProfile();
    auto pack = std::make_shared<bake::RuntimeAssetPack>();
    Require(pack->Mount(outputPack, profile) == bake::RuntimeAssetPackStatus::Success,
        "Authoritative sourceGraph package did not mount");
    kb::assets::AssetManager runtimeManager;
    Require(runtimeManager.RegisterLoader(std::make_unique<kb::render::RenderMaterialAssetLoader>()) &&
            runtimeManager.RegisterLoader(std::make_unique<kb::render::RenderMaterialGraphAssetLoader>()) &&
            runtimeManager.MountRuntimePack(pack),
        "Authoritative sourceGraph package did not register its runtime assets");
    const kb::assets::AssetHandle<kb::render::RenderMaterialAssetData> runtimeMaterial =
        runtimeManager.Load<kb::render::RenderMaterialAssetData>(materialId);
    Require(runtimeMaterial.IsLoaded(), "Cooked material source bytes did not load from the package");
    const kb::assets::AssetMetadata* runtimeMetadata = runtimeManager.Registry().Find(materialId);
    Require(runtimeMetadata != nullptr, "Cooked material metadata was absent from the package");
    const kb::render::RenderMaterialSourceGraphResolveResult runtimeGraph =
        kb::render::ResolveRenderMaterialSourceGraph(runtimeManager, *runtimeMetadata, *runtimeMaterial);
    Require(runtimeGraph.graph.has_value(), "Packaged runtime could not resolve sourceGraph B");
    const kb::render::RenderMaterialGraphCompileResult runtimeCompile =
        kb::render::CompileRenderMaterialGraphToShaderSource(*runtimeGraph.graph);
    Require(runtimeCompile.Succeeded() &&
            runtimeCompile.shader.sourceHash == authoritativeCompile.shader.sourceHash &&
            runtimeCompile.shader.sourceHash != inlineCompile.shader.sourceHash,
        "Packaged runtime resolved stale inline graph A instead of sourceGraph B");

    std::string providerError;
    const std::shared_ptr<kb::render::RuntimeAssetShaderProvider> provider =
        kb::render::RuntimeAssetShaderProvider::Create(pack, providerError);
    std::vector<std::uint8_t> shaderBytes;
    std::uint64_t revision = 0U;
    Require(provider != nullptr && provider->ReadMaterialShader(
                authoritativeCompile.shader.sourceHash,
                authoritativeVariant,
                "BaseOpaque",
                bgfx::RendererType::OpenGLES,
                "fragment",
                shaderBytes,
                revision) &&
            !shaderBytes.empty() && revision != 0U,
        "Runtime shader provider could not read sourceGraph B's cooked material shader");
    Require(!provider->ReadMaterialShader(
                inlineCompile.shader.sourceHash,
                kb::render::ComputeRenderMaterialGraphVariantKey(inlineCompile.shader),
                "BaseOpaque",
                bgfx::RendererType::OpenGLES,
                "fragment",
                shaderBytes,
                revision),
        "Cooked package unexpectedly contains stale inline graph A's shader");
    pack->Unmount();

    const std::filesystem::path webGpuPackPath = fixture.root / "GameWebGPU.kbpack";
    std::ostringstream webGpuDiagnostics;
    const kb::game::ProjectCookResult webGpuCook = kb::game::CookProject(
        kb::game::ProjectCookRequest{
            .projectPath = fixture.root,
            .targetProfileId = "WebGPU.wasm32",
            .outputPackPath = webGpuPackPath,
        },
        webGpuDiagnostics);
    Require(webGpuCook.succeeded, webGpuCook.error.c_str());
    Require(webGpuCook.shaderArtifactCount != 0U &&
            std::filesystem::is_regular_file(webGpuPackPath),
        "CookProject failed to publish the WebGPU WGSL package");

    const bake::BakeTargetProfile webGpuProfile = bake::WebGpuWasm32BakeTargetProfile();
    auto webGpuPack = std::make_shared<bake::RuntimeAssetPack>();
    Require(webGpuPack->Mount(webGpuPackPath, webGpuProfile) ==
            bake::RuntimeAssetPackStatus::Success,
        "WebGPU package did not mount under its exact target profile");
    providerError.clear();
    const std::shared_ptr<kb::render::RuntimeAssetShaderProvider> webGpuProvider =
        kb::render::RuntimeAssetShaderProvider::Create(webGpuPack, providerError);
    shaderBytes.clear();
    revision = 0U;
    Require(webGpuProvider != nullptr && webGpuProvider->ReadMaterialShader(
                authoritativeCompile.shader.sourceHash,
                authoritativeVariant,
                "BaseOpaque",
                bgfx::RendererType::WebGPU,
                "fragment",
                shaderBytes,
                revision) &&
            !shaderBytes.empty() && revision != 0U,
        "Runtime shader provider could not read the cooked WGSL material shader");
    webGpuPack->Unmount();

    const Fixture missing = BuildFixture(TestRoot() / "missing_authoritative_graph_cook", "Project");
    authorMaterial(missing, inlineGraph, "/Game/Materials/Missing.kbmaterialgraph", {});
    authorMaterialScene(missing, materialId);
    WriteSettings(missing.root, settings);
    const std::filesystem::path rejectedPack = missing.root / "must_not_exist.kbpack";
    std::ostringstream rejectedDiagnostics;
    const kb::game::ProjectCookResult rejected = kb::game::CookProject(
        kb::game::ProjectCookRequest{
            .projectPath = missing.root,
            .targetProfileId = "WebGL.wasm32",
            .outputPackPath = rejectedPack,
        },
        rejectedDiagnostics);
    Require(!rejected.succeeded && Mentions(rejected.error, "sourceGraph") &&
            !std::filesystem::exists(rejectedPack),
        "CookProject published a material whose path-only sourceGraph was missing");

    const Fixture missingCollection =
        BuildFixture(TestRoot() / "missing_graph_collection_cook", "Project");
    const kb::assets::AssetId missingCollectionId = kb::assets::MakeAssetId(
        "/Game/Materials/Missing.kbmpc:" +
        std::string{ kb::render::kRenderMaterialParameterCollectionAssetType });
    kb::render::RenderMaterialGraphDocument collectionGraph = authoritativeGraph;
    collectionGraph.nodes.push_back(kb::render::RenderMaterialGraphNode{
        .id = 3U,
        .kind = kb::render::RenderMaterialGraphNodeKind::CollectionParameter,
        .parameter = kb::render::RenderMaterialGraphParameterMetadata{
            .stableId = "GlobalTint",
            .defaultValueHint = std::to_string(missingCollectionId.value),
        },
    });
    const std::filesystem::path collectionGraphPath =
        missingCollection.root / "Assets" / "Materials" / "Authoritative.kbmaterialgraph";
    directoryError.clear();
    std::filesystem::create_directories(collectionGraphPath.parent_path(), directoryError);
    Require(!directoryError && kb::render::RenderMaterialGraphAssetLoader::SaveGraph(
                collectionGraphPath, collectionGraph),
        "Missing-MPC fixture could not write its authoritative graph");
    authorMaterial(missingCollection, inlineGraph, graphVirtualPath, {});
    authorMaterialScene(missingCollection, materialId);
    WriteSettings(missingCollection.root, settings);
    const std::filesystem::path missingCollectionPack =
        missingCollection.root / "must_not_exist.kbpack";
    std::ostringstream collectionDiagnostics;
    const kb::game::ProjectCookResult collectionCook = kb::game::CookProject(
        kb::game::ProjectCookRequest{
            .projectPath = missingCollection.root,
            .targetProfileId = "WebGL.wasm32",
            .outputPackPath = missingCollectionPack,
        },
        collectionDiagnostics);
    Require(!collectionCook.succeeded && Mentions(collectionCook.error, "parameter collection") &&
            !std::filesystem::exists(missingCollectionPack),
        "CookProject published a graph whose required parameter collection was missing");

    const Fixture missingCollectionMember =
        BuildFixture(TestRoot() / "missing_graph_collection_member_cook", "Project");
    const std::string collectionVirtualPath = "/Game/Materials/Globals.kbmpc";
    const kb::assets::AssetId collectionId = kb::assets::MakeAssetId(
        collectionVirtualPath + ":" +
        std::string{ kb::render::kRenderMaterialParameterCollectionAssetType });
    kb::render::RenderMaterialParameterCollectionData collection{};
    collection.parameters.push_back(kb::render::RenderMaterialParameterCollectionParameter{
        .stableId = "Exposure",
        .displayName = "Exposure",
        .type = kb::render::RenderMaterialParameterCollectionValueType::Scalar,
        .defaultValue = { 1.0F, 0.0F, 0.0F, 0.0F },
    });
    const std::filesystem::path collectionPath =
        missingCollectionMember.root / "Assets" / "Materials" / "Globals.kbmpc";
    directoryError.clear();
    std::filesystem::create_directories(collectionPath.parent_path(), directoryError);
    Require(!directoryError && kb::render::RenderMaterialParameterCollectionWriter::Save(
                collectionPath, collection),
        "Missing-MPC-member fixture could not write its parameter collection");

    kb::render::RenderMaterialGraphDocument missingMemberGraph =
        kb::render::MakeDefaultRenderMaterialGraphDocument();
    missingMemberGraph.storageModel = "material-graph-asset";
    missingMemberGraph.nodes.push_back(kb::render::RenderMaterialGraphNode{
        .id = 2U,
        .kind = kb::render::RenderMaterialGraphNodeKind::CollectionParameter,
        .parameter = kb::render::RenderMaterialGraphParameterMetadata{
            .stableId = "MissingTint",
            .defaultValueHint = std::to_string(collectionId.value),
        },
    });
    kb::render::RenderMaterialGraphLink collectionOutputLink{
        .fromNodeId = 2U,
        .fromPinId = kb::render::RenderMaterialGraphStablePinId(
            kb::render::RenderMaterialGraphNodeKind::CollectionParameter, "rgba", true),
        .fromPin = "rgba",
        .toNodeId = 1U,
        .toPinId = kb::render::RenderMaterialGraphStablePinId(
            kb::render::RenderMaterialGraphNodeKind::MaterialOutput, "baseColor", false),
        .toPin = "baseColor",
    };
    collectionOutputLink.id = kb::render::MakeRenderMaterialGraphLinkId(collectionOutputLink);
    missingMemberGraph.links.push_back(collectionOutputLink);
    const std::filesystem::path missingMemberGraphPath =
        missingCollectionMember.root / "Assets" / "Materials" / "Authoritative.kbmaterialgraph";
    Require(kb::render::RenderMaterialGraphAssetLoader::SaveGraph(
                missingMemberGraphPath, missingMemberGraph),
        "Missing-MPC-member fixture could not write its authoritative graph");
    authorMaterial(missingCollectionMember, inlineGraph, graphVirtualPath, {});
    authorMaterialScene(missingCollectionMember, materialId);
    WriteSettings(missingCollectionMember.root, settings);
    const std::filesystem::path missingCollectionMemberPack =
        missingCollectionMember.root / "must_not_exist.kbpack";
    std::ostringstream missingMemberDiagnostics;
    const kb::game::ProjectCookResult missingMemberCook = kb::game::CookProject(
        kb::game::ProjectCookRequest{
            .projectPath = missingCollectionMember.root,
            .targetProfileId = "WebGL.wasm32",
            .outputPackPath = missingCollectionMemberPack,
        },
        missingMemberDiagnostics);
    Require(!missingMemberCook.succeeded && Mentions(missingMemberCook.error, "MissingTint") &&
            Mentions(missingMemberCook.error, "parameter collection") &&
            !std::filesystem::exists(missingCollectionMemberPack),
        "CookProject published a graph whose parameter collection member was missing");
}

// -------------------------------------------------------------------------
// Reading the project: settings, the legacy fallback, plugins.
// -------------------------------------------------------------------------

void RunSettingsTests() {
    const Fixture fixture = BuildFixture(TestRoot() / "settings", "Project");
    kb::project::ProjectSettings settings;
    settings.name = "InternalName";
    settings.gameName = "Shipped Name";
    settings.defaultMap = "/Game/Scenes/Main.21kbscene";
    settings.physicsLayersAsset = "/Game/Config/Layers.21kbphysicslayers";
    settings.inputMappingContext = "/Game/Input/Context.21kbinput";
    settings.inputEnabled = false;
    WriteSettings(fixture.root, settings);

    std::ostringstream err;
    kb::game::GameProjectRuntime runtime{};
    Require(
        kb::game::ReadGameProjectRuntime(fixture.root, "", runtime, err),
        "A project with a settings file must be readable");
    Require(runtime.gameName == "Shipped Name", "The game ships under gameName, not the project name");
    Require(
        runtime.sceneReference == "/Game/Scenes/Main.21kbscene",
        "An empty scene override must leave ProjectSettings::defaultMap in place");
    Require(
        runtime.physicsLayersAsset == "/Game/Config/Layers.21kbphysicslayers",
        "The physics layers asset must reach the runtime");
    Require(
        runtime.inputMappingContext == "/Game/Input/Context.21kbinput",
        "The input mapping context must reach the runtime");
    Require(!runtime.inputEnabled, "An input-disabled project must reach the runtime disabled");
    Require(
        runtime.projectRoot == std::filesystem::absolute(fixture.root).lexically_normal(),
        "The project root is the directory holding the descriptor");

    kb::game::GameProjectRuntime overridden{};
    Require(
        kb::game::ReadGameProjectRuntime(
            fixture.root, "/Game/Scenes/Other.21kbscene", overridden, err),
        "A project with a scene override must be readable");
    Require(
        overridden.sceneReference == "/Game/Scenes/Other.21kbscene",
        "A scene override must beat ProjectSettings::defaultMap");

    // The settings file is what the editor writes; the descriptor is not.
    kb::project::ProjectSettings renamed = settings;
    renamed.gameName.clear();
    renamed.name = "FallsBackToName";
    WriteSettings(fixture.root, renamed);
    kb::game::GameProjectRuntime unnamed{};
    Require(
        kb::game::ReadGameProjectRuntime(fixture.root, "", unnamed, err),
        "A project without a gameName must still be readable");
    Require(
        unnamed.gameName == "FallsBackToName",
        "A project with no gameName ships under its project name");
}

void RunLegacySettingsFallbackTests() {
    // No Config/ProjectSettings.ini at all: a package built before the settings
    // file existed still has to come up, carrying whatever the descriptor knows.
    // The fallback names the project after its own file; dropping it leaves the
    // default-constructed name behind instead.
    const Fixture fixture = BuildFixture(TestRoot() / "legacy", "LegacyFallbackGame");
    Require(
        !std::filesystem::exists(kb::project::ProjectSettingsStore::FilePath(fixture.root)),
        "The legacy fallback case must have no settings file");

    // Named by its descriptor rather than its directory, which is also the only
    // way a project whose file is not called Project.21kbproject can be opened.
    const std::filesystem::path descriptorPath =
        fixture.root / "LegacyFallbackGame.21kbproject";
    std::ostringstream err;
    kb::game::GameProjectRuntime runtime{};
    Require(
        kb::game::ReadGameProjectRuntime(descriptorPath, "", runtime, err),
        "A project with no settings file must still be readable");
    Require(
        runtime.gameName == "LegacyFallbackGame",
        "A project with no settings file must fall back to the settings its descriptor carries");
    Require(
        runtime.sceneReference == kb::project::ProjectSettings{}.defaultMap,
        "The fallback still has to produce a scene to start from");
}

void RunPluginTests() {
    const Fixture fixture = BuildFixture(TestRoot() / "plugins", "Project");
    WriteSettings(fixture.root, kb::project::ProjectSettings{});
    WriteTextFile(fixture.root / "Binaries" / "local.dll", "not really a library");

    const std::filesystem::path absoluteElsewhere =
        std::filesystem::absolute(TestRoot() / "plugins" / "Binaries" / "local.dll");

    kb::project::ProjectDescriptor descriptor;
    descriptor.plugins = {
        kb::project::ProjectPluginReference{
            .name = "LocalPlugin", .binaryPath = "Binaries/local.dll", .enabled = true },
        kb::project::ProjectPluginReference{
            .name = "PortablePlugin", .binaryPath = "portable_only.dll", .enabled = true },
        kb::project::ProjectPluginReference{
            .name = "DisabledPlugin", .binaryPath = "Binaries/local.dll", .enabled = false },
        kb::project::ProjectPluginReference{
            .name = "AbsolutePlugin",
            .binaryPath = absoluteElsewhere.string(),
            .enabled = true },
    };
    Require(
        kb::project::ProjectManager::SaveProject(
            fixture.root / "Project.21kbproject", descriptor),
        "kb_game_core plugin descriptor could not be written");

    std::ostringstream err;
    kb::game::GameProjectRuntime runtime{};
    Require(
        kb::game::ReadGameProjectRuntime(fixture.root, "", runtime, err),
        "A project with plugins must be readable");

    // Every enabled plugin is a module the game will refuse to run without, and
    // a disabled one is not.
    Require(
        runtime.requiredModules ==
            std::vector<std::string>{ "LocalPlugin", "PortablePlugin", "AbsolutePlugin" },
        "Exactly the enabled plugins are required modules, in descriptor order");

    const std::filesystem::path expectedLocal =
        std::filesystem::absolute(fixture.root).lexically_normal() / "Binaries" / "local.dll";
    Require(
        runtime.descriptor.plugins.size() == 4U,
        "The descriptor handed to the scene keeps every plugin it was given");
    Require(
        std::filesystem::path{ runtime.descriptor.plugins[0].binaryPath } == expectedLocal,
        "A relative plugin binary that is packaged beside the project is resolved against it");
    Require(
        runtime.descriptor.plugins[1].binaryPath == "portable_only.dll",
        "A relative plugin binary that is not packaged keeps its portable filename");
    Require(
        runtime.descriptor.plugins[2].binaryPath == "Binaries/local.dll",
        "A disabled plugin is left exactly as the descriptor stored it");
    Require(
        runtime.descriptor.plugins[3].binaryPath == absoluteElsewhere.string(),
        "An absolute plugin binary is never rewritten");
}

void RunMissingProjectTests() {
    std::ostringstream err;
    kb::game::GameProjectRuntime runtime{};
    Require(
        !kb::game::ReadGameProjectRuntime(TestRoot() / "no_such_project", "", runtime, err),
        "A project directory that does not exist must be refused");
    Require(
        Mentions(err.str(), "project descriptor was not found"),
        "A missing descriptor must be named");

    const Fixture fixture = BuildFixture(TestRoot() / "notaproject", "Project");
    std::ostringstream fileErr;
    kb::game::GameProjectRuntime fromFile{};
    Require(
        !kb::game::ReadGameProjectRuntime(
            fixture.root / "Assets" / "Logic" / "Player.lua", "", fromFile, fileErr),
        "A file that is not a project descriptor must be refused");
    Require(!fileErr.str().empty(), "Refusing a file that is not a descriptor must say so");
}

// -------------------------------------------------------------------------
// Bringing the scene up: modules, physics layers, input, scene resolution.
// -------------------------------------------------------------------------

[[nodiscard]] kb::game::GameProjectRuntime RuntimeFor(const Fixture& fixture) {
    kb::game::GameProjectRuntime runtime{};
    runtime.projectRoot = std::filesystem::absolute(fixture.root).lexically_normal();
    runtime.sceneReference = fixture.sceneVirtualPath;
    return runtime;
}

void RunSceneLoadTests() {
    const Fixture fixture = BuildFixture(TestRoot() / "scene", "Project");

    {
        kb::scene::Scene scene;
        std::ostringstream err;
        std::filesystem::path loaded;
        std::size_t discovered = 0U;
        Require(
            kb::game::LoadGameProjectScene(RuntimeFor(fixture), scene, loaded, discovered, err),
            "A project scene named by its virtual path must load");
        Require(discovered > 0U, "Mounting and discovering a project must find its assets");
        Require(
            loaded.filename() == "Main.21kbscene",
            "The physical scene file the registry named is the one that is read");
        Require(scene.Entities().Count() == 3U, "The scene has to be instantiated, not just read");
    }

    {
        // A reference that is not a virtual path is resolved under the project.
        kb::scene::Scene scene;
        kb::game::GameProjectRuntime runtime = RuntimeFor(fixture);
        runtime.sceneReference = "Assets/Scenes/Main.21kbscene";
        std::ostringstream err;
        std::filesystem::path loaded;
        std::size_t discovered = 0U;
        Require(
            kb::game::LoadGameProjectScene(runtime, scene, loaded, discovered, err),
            "A project-relative scene path must load");
        Require(
            loaded == runtime.projectRoot / "Assets" / "Scenes" / "Main.21kbscene",
            "A relative scene path is resolved against the project root");
    }

    {
        kb::scene::Scene scene;
        kb::game::GameProjectRuntime runtime = RuntimeFor(fixture);
        runtime.sceneReference = "/Game/Scenes/Absent.21kbscene";
        std::ostringstream err;
        std::filesystem::path loaded;
        std::size_t discovered = 0U;
        Require(
            !kb::game::LoadGameProjectScene(runtime, scene, loaded, discovered, err),
            "A scene asset that does not exist must be refused");
        Require(
            Mentions(err.str(), "project scene asset was not found"),
            "A missing scene asset must be named");
    }

    {
        // A configured module that did not come up is a game running without a
        // piece of itself; it must not start.
        kb::scene::Scene scene;
        kb::game::GameProjectRuntime runtime = RuntimeFor(fixture);
        runtime.requiredModules = { "APluginThatIsNotHere" };
        std::ostringstream err;
        std::filesystem::path loaded;
        std::size_t discovered = 0U;
        Require(
            !kb::game::LoadGameProjectScene(runtime, scene, loaded, discovered, err),
            "A configured module that is not active must stop the game");
        Require(
            Mentions(err.str(), "configured module is not active") &&
                Mentions(err.str(), "APluginThatIsNotHere"),
            "An inactive configured module must be named");
        Require(
            scene.Entities().Count() == 0U,
            "A refusal over a module must happen before the scene is brought up");
    }

    {
        kb::scene::Scene scene;
        kb::game::GameProjectRuntime runtime = RuntimeFor(fixture);
        runtime.physicsLayersAsset = "/Game/Config/Absent.21kbphysicslayers";
        std::ostringstream err;
        std::filesystem::path loaded;
        std::size_t discovered = 0U;
        Require(
            !kb::game::LoadGameProjectScene(runtime, scene, loaded, discovered, err),
            "Physics layers the project names but does not have must stop the game");
        Require(
            Mentions(err.str(), "project physics layers could not be applied"),
            "Unapplied physics layers must be named");
        Require(
            scene.Entities().Count() == 0U,
            "A refusal over physics layers must happen before the scene is brought up");
    }

    {
        // The mapping context is checked for its type, not just its presence: a
        // project pointing at the wrong asset must be told, not quietly ignored.
        kb::scene::Scene scene;
        kb::game::GameProjectRuntime runtime = RuntimeFor(fixture);
        runtime.inputEnabled = true;
        runtime.inputMappingContext = fixture.behaviourVirtualPath;
        std::ostringstream err;
        std::filesystem::path loaded;
        std::size_t discovered = 0U;
        Require(
            !kb::game::LoadGameProjectScene(runtime, scene, loaded, discovered, err),
            "An input mapping context that is not an input mapping context must stop the game");
        Require(
            Mentions(err.str(), "project input mapping could not be activated"),
            "An unactivatable input mapping must be named");
    }

    {
        // ... and the project's own switch has to be honoured.
        kb::scene::Scene scene;
        kb::game::GameProjectRuntime runtime = RuntimeFor(fixture);
        runtime.inputEnabled = false;
        runtime.inputMappingContext = fixture.behaviourVirtualPath;
        std::ostringstream err;
        std::filesystem::path loaded;
        std::size_t discovered = 0U;
        Require(
            kb::game::LoadGameProjectScene(runtime, scene, loaded, discovered, err),
            "A project with input disabled must not be stopped by its mapping context");
    }

    {
        // The project's context has to be on the stack when the loop starts, and
        // it has to survive SceneInputActivation, which clears every local user's
        // contexts before it re-adds the ones the scene's own entities ask for.
        // Ordering these the other way around leaves the game with no bindings.
        Require(
            kb::input::WriteInputMappingContext(
                fixture.root / "Assets" / "Input" / "Player.21kbinputcontext",
                kb::input::InputMappingContextAsset{}),
            "kb_game_core test input mapping context could not be written");

        kb::scene::Scene scene;
        kb::game::GameProjectRuntime runtime = RuntimeFor(fixture);
        runtime.inputEnabled = true;
        runtime.inputMappingContext = "/Game/Input/Player.21kbinputcontext";
        std::ostringstream err;
        std::filesystem::path loaded;
        std::size_t discovered = 0U;
        Require(
            kb::game::LoadGameProjectScene(runtime, scene, loaded, discovered, err),
            "A project naming a real input mapping context must come up");
        const kb::assets::AssetMetadata* context =
            scene.Assets().Manager().Registry().FindByPath(runtime.inputMappingContext);
        Require(context != nullptr, "The input mapping context must have been discovered");
        Require(
            scene.Input().HasMappingContext(context->id.value),
            "The project's input mapping context must still be active when the loop starts");
    }
}

} // namespace

int main(int argc, char** argv) {
    std::error_code error;
    std::filesystem::create_directories(TestRoot(), error);
    Require(!error, "kb_game_core test root could not be prepared");

    if (argc == 2 && std::string_view{ argv[1] } == "--windows-runtime-modules") {
        RunPackagedRuntimeModuleContractTests();
        RunWindowsRuntimeModulePackagingTests();
        std::fputs("kb_game_core Windows runtime-module tests passed\n", stdout);
        return EXIT_SUCCESS;
    }
    Require(argc == 1, "kb_game_core tests received an unsupported argument");

    RunRuntimeDeltaTests();
    RunPackagedRuntimeModuleContractTests();
    RunCookOutputLockTest();
    RunWindowsRuntimeModulePackagingTests();
    RunSceneMetaCookValidationTests();
    RunAuthoritativeMaterialGraphCookTest();
    RunNarrowingTests();
    RunSettingsTests();
    RunLegacySettingsFallbackTests();
    RunPluginTests();
    RunMissingProjectTests();
    RunSceneLoadTests();

    std::fputs("kb_game_core tests passed\n", stdout);
    return EXIT_SUCCESS;
}
