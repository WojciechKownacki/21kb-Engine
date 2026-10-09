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

#include "engine/platform/FileSystemPath.hpp"

#include "engine/assets/AssetManager.hpp"
#include "engine/assets/AssetMetadata.hpp"
#include "engine/assets/AssetRegistry.hpp"
#include "engine/assets/bake/BakeTargetProfile.hpp"
#include "engine/assets/bake/AssetPackReader.hpp"
#include "engine/assets/bake/AssetPackSeal.hpp"
#include "engine/assets/bake/AssetPackTools.hpp"
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
#include "engine/scene/SceneUI.hpp"
#include "engine/scene/MeshRendererComponent.hpp"
#include "engine/scene/SceneNavigation.hpp"
#include "engine/scene/SceneRuntime.hpp"
#include "engine/world/WorldCellIndex.hpp"
#include "engine/world/WorldDescriptor.hpp"
#include "engine/world/WorldObjectFile.hpp"
#include "engine/world/WorldPackChunks.hpp"
#include "engine/world/WorldPartitionRuntime.hpp"
#include "CliCommands.hpp"
#include "engine/security/ReleaseKeys.hpp"
#include "engine/security/ReleaseManifest.hpp"
#include "engine/save/SaveGameService.hpp"
#include "engine/script/ScriptAsset.hpp"
#include "engine/script/ScriptRuntimeHost.hpp"
#include "kb/render/RuntimeAssetShaderProvider.hpp"
#include "kb/render/resources/RenderMaterialAssetLoader.hpp"
#include "kb/render/resources/RenderTextureAssetLoader.hpp"
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
#include <thread>
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

void RunTextureReuseCookTests() {
    namespace bake = kb::assets::bake;
    const Fixture fixture = BuildFixture(TestRoot() / "texture_reuse", "Project");
    kb::project::ProjectSettings settings;
    settings.defaultMap = fixture.sceneVirtualPath;
    settings.physicsLayersAsset.clear();
    settings.inputEnabled = false;
    WriteSettings(fixture.root, settings);
    const auto texturePath = fixture.root / "Assets/Textures/Reuse.tga";
    std::string texture(18U + 4U * 4U * 3U, '\0');
    texture[2] = 2; texture[12] = 4; texture[14] = 4; texture[16] = 24;
    WriteTextFile(texturePath, texture);
    {
        kb::scene::Scene scene;
        Require(scene.Assets().Manager().RegisterLoader(std::make_unique<kb::render::RenderTextureAssetLoader>()),
            "Texture reuse fixture loader registration failed");
        Require(scene.Assets().MountProject(fixture.root), "Texture reuse fixture mount failed");
        static_cast<void>(scene.Assets().Discover());
        const auto* metadata = scene.Assets().Manager().Registry().FindByPath("/Game/Textures/Reuse.tga");
        Require(metadata != nullptr, "Texture reuse fixture texture was not discovered");
        const auto object = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{ .name = "Image" });
        scene.Components().UI().Set(object.Entity(), kb::scene::UIRawImage{ .imageAssetId = metadata->id.value });
        Require(kb::scene::SceneDocumentService::Save(scene,
            fixture.root / "Assets/Scenes/Main.21kbscene", "Main"), "Texture reuse scene save failed");
    }
    kb::game::ProjectCookRequest request{
        .projectPath = fixture.root, .targetProfileId = "Windows.x64",
        .outputPackPath = fixture.root / "Game.kbpack",
    };
    auto cook = [&]() {
        std::ostringstream diagnostics;
        const auto result = kb::game::CookProject(request, diagnostics);
        if (!result.succeeded) std::fprintf(stderr, "%s\n", result.error.c_str());
        return std::pair{ result, diagnostics.str() };
    };
    const auto cold = cook();
    Require(cold.first.succeeded && cold.first.textureArtifactCount > 0U &&
        Mentions(cold.second, "cooking texture:"), "Cold texture cook failed");
    const auto original = fixture.root / "Original.kbpack";
    std::filesystem::copy_file(request.outputPackPath, original);
    request.reusePackPath = request.outputPackPath;
    const auto warm = cook();
    Require(warm.first.succeeded && Mentions(warm.second, "reusing texture:") &&
        !Mentions(warm.second, "cooking texture:"), "Same-output texture reuse failed");
    std::uint64_t corruptOffset = 0U;
    {
        bake::AssetPackReader before, after;
        Require(before.Mount(original) == bake::AssetPackReadStatus::Success &&
            after.Mount(request.outputPackPath) == bake::AssetPackReadStatus::Success,
            "Texture reuse packages could not be mounted");
        for (const auto& artifact : before.Artifacts()) {
            if (artifact.assetTypeId != "Texture2D") continue;
            const auto* reused = after.FindArtifact(artifact.key);
            std::vector<std::uint8_t> a, b;
            Require(reused != nullptr && before.ReadBlock(artifact, bake::kBakedAssetPrimaryBlockName, a) ==
                bake::AssetPackReadStatus::Success && after.ReadBlock(*reused, bake::kBakedAssetPrimaryBlockName, b) ==
                bake::AssetPackReadStatus::Success && a == b, "Reused texture bytes or key changed");
            corruptOffset = artifact.blocks.front().offset;
        }
    }
    Require(corruptOffset != 0U, "Texture reuse fixture contains no texture blocks");
    {
        std::fstream corrupt{ original, std::ios::binary | std::ios::in | std::ios::out };
        corrupt.seekg(static_cast<std::streamoff>(corruptOffset));
        char byte = 0;
        corrupt.read(&byte, 1);
        byte ^= 1;
        corrupt.seekp(static_cast<std::streamoff>(corruptOffset));
        corrupt.write(&byte, 1);
        Require(corrupt.good(), "Texture cache corruption fixture failed");
    }
    const auto readOutput = [&]() {
        std::ifstream input{ request.outputPackPath, std::ios::binary };
        return std::string{ std::istreambuf_iterator<char>{ input }, std::istreambuf_iterator<char>{} };
    };
    const auto published = readOutput();
    request.targetProfileId = "Linux.x64";
    const auto incompatible = cook();
    Require(!incompatible.first.succeeded && Mentions(incompatible.first.error, "incompatible with the target profile") &&
        readOutput() == published, "Incompatible texture cache replaced a valid published package");
    request.targetProfileId = "Windows.x64";
    request.reusePackPath = original;
    const auto corrupt = cook();
    Require(!corrupt.first.succeeded && Mentions(corrupt.first.error, "cached texture artifact is invalid") &&
        readOutput() == published, "Corrupt cached texture replaced a valid published package");
    request.reusePackPath = fixture.root / "Invalid.kbpack";
    WriteTextFile(request.reusePackPath, "invalid cache");
    Require(!cook().first.succeeded && readOutput() == published,
        "Invalid reuse package replaced a valid published package");
    request.reusePackPath = request.outputPackPath;
    texture[18] = 127;
    WriteTextFile(texturePath, texture);
    const auto changed = cook();
    Require(changed.first.succeeded && Mentions(changed.second, "cooking texture:") &&
        !Mentions(changed.second, "reusing texture:"), "Changed texture reused stale bytes");
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

// The output package goes wherever the user points the Build Game panel, and Win32 stops an
// ordinary path at MAX_PATH. The publication lock is a raw CreateFileW, so a deep output turned
// into "already being cooked" - a message that sends the reader looking for another cook that was
// never running.
void RunDeepOutputPathCookTest() {
    const Fixture fixture = BuildFixture(TestRoot() / "deep_output_cook", "Project");
    kb::project::ProjectSettings settings;
    settings.defaultMap = fixture.sceneVirtualPath;
    settings.physicsLayersAsset.clear();
    settings.inputEnabled = false;
    WriteSettings(fixture.root, settings);

    std::filesystem::path outputDirectory = fixture.root / "published";
    while (outputDirectory.string().size() < 260U) {
        outputDirectory /= "player_build_output_folder";
    }
    std::error_code error;
    std::filesystem::create_directories(kb::platform::ExtendedLengthPath(outputDirectory), error);
    Require(!error, "Deep output fixture directory could not be created");
    const std::filesystem::path outputPack = outputDirectory / "Game.kbpack";
    Require(outputPack.string().size() > 260U,
        "This test only proves anything while the output path is longer than MAX_PATH");

    std::ostringstream diagnostics;
    const kb::game::ProjectCookResult cook = kb::game::CookProject(
        kb::game::ProjectCookRequest{
            .projectPath = fixture.root,
            .targetProfileId = "Windows.x64",
            .outputPackPath = outputPack,
        },
        diagnostics);
    const std::string deepFailure =
        "CookProject must publish into an output path past MAX_PATH: " + cook.error;
    Require(cook.succeeded, deepFailure.c_str());
    const std::filesystem::path published = kb::platform::ExtendedLengthPath(outputPack);
    Require(std::filesystem::is_regular_file(published, error) &&
            std::filesystem::file_size(published, error) > 0U,
        "A package published past MAX_PATH must be on disk whole");
}

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

void RunNativeBehaviourPackagingTests() {
    namespace bake = kb::assets::bake;
    const Fixture fixture = BuildFixture(TestRoot() / "native_script_project", "Project");
    kb::project::ProjectSettings settings;
    settings.defaultMap = fixture.sceneVirtualPath;
    settings.physicsLayersAsset.clear();
    settings.inputEnabled = false;
    WriteSettings(fixture.root, settings);
    WriteTextFile(fixture.root / "Assets" / "Player.native",
        "name = Player\nsymbol = Tests.PackagedNative\nmodule = ../Binaries/native.dll\n"
        "source = ../Source/editor-only.cpp\nentry = kb_register_native_scripts\n");
    std::filesystem::create_directories(fixture.root / "Binaries");
    std::filesystem::copy_file(KB_WINDOWS_RUNTIME_MODULE_TEST_PLUGIN_PATH,
        fixture.root / "Binaries" / "native.dll");
    kb::assets::AssetId nativeId;
    {
        kb::scene::Scene authored{kb::scene::SceneMode::PrefabPrivate};
        Require(authored.Assets().MountProject(fixture.root), "Native fixture mount failed");
        static_cast<void>(authored.Assets().Discover());
        const auto* asset = authored.Assets().Manager().Registry().FindByPath("/Game/Player.native");
        Require(asset != nullptr, "Native fixture descriptor was not discovered");
        nativeId = asset->id;
        const auto entity = authored.Entities().CreateEntity();
        authored.Components().Behaviours().Set(entity, {
            .behaviourAssetId = nativeId.value, .backend = kb::scene::BehaviourBackend::Native,
        });
        Require(kb::scene::SceneDocumentService::Save(authored,
            fixture.root / "Assets" / "Scenes" / "Main.21kbscene", "Main"), "Native fixture scene save failed");
    }
    const auto sealedRoot = TestRoot() / "native_script_package";
    const auto packPath = sealedRoot / "Game.kbpack";
    std::ostringstream diagnostics;
    const kb::game::ProjectCookRequest request{
        .projectPath = fixture.root,
        .targetProfileId = "Windows.x64",
        .outputPackPath = packPath,
        .runtimeModulesOutputDirectory = sealedRoot / "RuntimeModules",
    };
    const auto cooked = kb::game::CookProject(request, diagnostics);
    Require(cooked.succeeded, cooked.error.c_str());
    Require(std::filesystem::remove(fixture.root / "Binaries" / "native.dll"),
        "Native fixture authoring DLL removal failed");
    auto pack = std::make_shared<bake::RuntimeAssetPack>();
    Require(pack->Mount(packPath, bake::WindowsX64BakeTargetProfile()) == bake::RuntimeAssetPackStatus::Success,
        "Native package could not be mounted");
    {
        kb::scene::Scene runtime{kb::scene::SceneMode::PrefabPrivate};
        Require(runtime.Assets().Manager().MountRuntimePack(pack), "Native runtime registry mount failed");
        const auto descriptor = runtime.Assets().Manager().Load<kb::script::NativeBehaviourDescriptor>(nativeId);
        Require(descriptor.IsLoaded() && descriptor->sourcePath.empty() && !descriptor->build.enabled &&
            !descriptor->shadowCopy && descriptor->modulePath == "RuntimeModules/Binaries/native.dll",
            "Native package retained authoring paths or lost its staged DLL reference");
        kb::script::ScriptRuntimeHost host{runtime};
        Require(host.Succeeded(), "Native fixture script host failed");
        host.AssetPreparer().SetNativeSettings({.buildPlugins = false, .runtimeModuleRoot = sealedRoot});
        const auto prepared = host.AssetPreparer().PrepareAsset(nativeId);
        Require(prepared.Succeeded(), prepared.diagnostics.empty() ? "Native preparation failed" : prepared.diagnostics.front().message.c_str());
        const auto entity = runtime.Entities().CreateEntity();
        kb::scene::BehaviourComponent behaviour{.behaviourAssetId = nativeId.value, .backend = kb::scene::BehaviourBackend::Native};
        kb::script::ScriptExecutionContext context{runtime, entity, nativeId, behaviour.backend,
            kb::script::ScriptLifecycleEvent::Ready, 0.0F, nullptr};
        Require(host.NativeBackend().ExecuteLifecycle(behaviour, context).Succeeded(), "Packaged native callback failed");
        const auto modulePath = std::filesystem::canonical(sealedRoot / descriptor->modulePath);
        const auto module = GetModuleHandleW(modulePath.c_str());
        Require(module != nullptr, "Native DLL was not loaded from the sealed runtime root");
        const auto calls = reinterpret_cast<std::uint32_t(*)()>(GetProcAddress(module, "kb_native_ready_count"));
        Require(calls != nullptr && calls() == 1U, "Packaged native callback did not execute exactly once");
    }
    pack->Unmount();
    const auto rejected = kb::game::CookProject(request, diagnostics);
    Require(!rejected.succeeded && Mentions(rejected.error, "native behaviour DLL"),
        "Native package accepted a missing DLL after authoring content was removed");
}

// A player with a trust anchor (a packaged release) mounts only packs sealed by its release key
// and names the reason when it refuses one; a player without an anchor accepts an unsigned pack;
// a damaged anchor is an error, never a development player.
void RunPackagedTrustTests() {
    namespace bake = kb::assets::bake;
    const Fixture fixture = BuildFixture(TestRoot() / "packaged_trust_project", "Project");
    kb::project::ProjectSettings settings;
    settings.defaultMap = fixture.sceneVirtualPath;
    settings.physicsLayersAsset.clear();
    settings.inputEnabled = false;
    WriteSettings(fixture.root, settings);
    const auto packPath = TestRoot() / "packaged_trust_package" / "Game.kbpack";
    std::ostringstream diagnostics;
    const auto cooked = kb::game::CookProject(
        kb::game::ProjectCookRequest{ .projectPath = fixture.root, .targetProfileId = "Windows.x64", .outputPackPath = packPath },
        diagnostics);
    Require(cooked.succeeded, cooked.error.c_str());

    kb::security::ReleaseSigningKey key;
    Require(kb::security::GenerateReleaseSigningKey(key), "Packaged trust test key could not be generated");
    kb::security::TrustAnchorLookup anchor{};
    bake::AssetPackTrust trust{};
    std::ostringstream err;
    Require(kb::game::ResolvePackagedAssetPackTrust(anchor, trust, err) && !trust.requiredSigner.has_value(),
        "A player without a trust anchor demanded a signed pack");
    anchor.state = kb::security::TrustAnchorLookup::State::Invalid;
    anchor.error = "damaged";
    Require(!kb::game::ResolvePackagedAssetPackTrust(anchor, trust, err) && Mentions(err.str(), "damaged"),
        "A damaged trust anchor was treated as a development player");
    anchor.state = kb::security::TrustAnchorLookup::State::Present;
    anchor.anchor.productId = "Publisher.Game";
    anchor.anchor.releaseKey = key.publicKey;
    Require(kb::game::ResolvePackagedAssetPackTrust(anchor, trust, err) && trust.requiredSigner == key.publicKey,
        "A packaged player did not require its release key");

    {
        bake::RuntimeAssetPack pack;
        const bake::RuntimeAssetPackStatus status = pack.Mount(
            packPath, bake::WindowsX64BakeTargetProfile(), bake::AssetPackAccess::Ranged, trust);
        Require(status == bake::RuntimeAssetPackStatus::ContainerRejected &&
                pack.ContainerStatus() == bake::AssetPackReadStatus::Unsigned,
            "A packaged player mounted an unsigned pack");
        std::ostringstream refusal;
        kb::game::ReportRuntimePackageRefusal(pack, status, refusal);
        Require(Mentions(refusal.str(), "Unsigned") && Mentions(refusal.str(), "not signed"),
            "The unsigned-pack refusal does not say why");
    }
    std::string error;
    Require(bake::SealAssetPack(packPath, key, nullptr, error), error.c_str());
    {
        bake::RuntimeAssetPack pack;
        Require(pack.Mount(packPath, bake::WindowsX64BakeTargetProfile(), bake::AssetPackAccess::Ranged, trust) ==
                bake::RuntimeAssetPackStatus::Success,
            "A packaged player refused a pack sealed by its release key");
    }
    kb::security::ReleaseSigningKey otherKey;
    Require(kb::security::GenerateReleaseSigningKey(otherKey), "Second packaged trust test key could not be generated");
    trust.requiredSigner = otherKey.publicKey;
    bake::RuntimeAssetPack foreign;
    const bake::RuntimeAssetPackStatus status = foreign.Mount(
        packPath, bake::WindowsX64BakeTargetProfile(), bake::AssetPackAccess::Ranged, trust);
    std::ostringstream refusal;
    kb::game::ReportRuntimePackageRefusal(foreign, status, refusal);
    Require(foreign.ContainerStatus() == bake::AssetPackReadStatus::UntrustedSigner &&
            Mentions(refusal.str(), "signed by a different key"),
        "A pack sealed by another key was not refused with its reason");

    // A packaged player binds saves to its game, not to one machine: a save it writes does not load
    // under the development key, and loads in any other copy of the same game, as a cloud save or a
    // save carried to another computer must.
    anchor.anchor.saveSecret = kb::security::DeriveGameSaveSecret(key, anchor.anchor.productId);
    std::ostringstream saveWarnings;
    kb::game::ConfigurePackagedSaveIntegrity(anchor.anchor, saveWarnings);
    Require(saveWarnings.str().empty(), "A packaged player could not configure its save key");
    kb::save::SaveGame save;
    save.SetInt("level", 3);
    const std::filesystem::path savePath = TestRoot() / "packaged_trust_save.kbsave";
    Require(kb::save::SaveGameService::Save(savePath, save), "A packaged save could not be written");
    Require(kb::save::SaveGameService::Load(savePath).Succeeded(), "A packaged save did not load in its game");
    kb::save::SaveGameService::ConfigureIntegrity(kb::save::DevelopmentSaveGameIntegrity());
    Require(kb::save::SaveGameService::Load(savePath).status == kb::save::SaveGameLoadStatus::Tampered,
        "A packaged save loaded without its game's secret");
    kb::security::TrustAnchor otherCopy = anchor.anchor;
    kb::game::ConfigurePackagedSaveIntegrity(otherCopy, saveWarnings);
    Require(kb::save::SaveGameService::Load(savePath).Succeeded(),
        "A packaged save did not load in another copy of the same game");
    kb::save::SaveGameService::ConfigureIntegrity(kb::save::DevelopmentSaveGameIntegrity());

    // A packaged release starts only from the files its signed manifest lists, binds its pack by
    // the pack's own seal, and refuses to go back to an older release when it asks for that.
    const std::filesystem::path releaseRoot = TestRoot() / "packaged_trust_release";
    const std::filesystem::path securityRoot = TestRoot() / "packaged_trust_security";
    std::filesystem::create_directories(releaseRoot);
    std::filesystem::copy_file(packPath, releaseRoot / "Game.kbpack");
    WriteTextFile(releaseRoot / "Game.exe", "player");
    const auto signRelease = [&](std::uint64_t number, bool antiRollback) {
        std::filesystem::remove(releaseRoot / "release.kbmanifest");
        kb::security::ReleaseManifest manifest{};
        manifest.productId = anchor.anchor.productId;
        manifest.contentVersion = "1.0.0";
        manifest.releaseNumber = number;
        manifest.antiRollback = antiRollback;
        std::string manifestError;
        Require(kb::security::BuildReleaseManifest(releaseRoot, manifest, manifestError), manifestError.c_str());
        WriteTextFile(releaseRoot / "release.kbmanifest", kb::security::SignReleaseManifest(manifest, key));
    };
    signRelease(5U, true);
    std::ostringstream releaseErrors;
    const auto release = kb::game::VerifyPackagedRelease(
        anchor.anchor, releaseRoot, releaseRoot / "Game.exe", securityRoot, releaseErrors);
    Require(release != nullptr && kb::security::CurrentVerifiedRelease() == release, releaseErrors.str().c_str());
    trust.requiredSigner = key.publicKey;
    {
        bake::RuntimeAssetPack pack;
        Require(pack.Mount(releaseRoot / "Game.kbpack", bake::WindowsX64BakeTargetProfile(),
                    bake::AssetPackAccess::Ranged, trust) == bake::RuntimeAssetPackStatus::Success &&
                kb::game::PackBelongsToRelease(*release, releaseRoot / "Game.kbpack", pack, releaseErrors),
            "The release's own pack was not bound to its manifest");
        Require(!kb::game::PackBelongsToRelease(*release, TestRoot() / "Elsewhere.kbpack", pack, releaseErrors),
            "A pack outside the release was bound to its manifest");
    }
    WriteTextFile(releaseRoot / "Game.exe", "playex");
    std::ostringstream modified;
    Require(kb::game::VerifyPackagedRelease(anchor.anchor, releaseRoot, releaseRoot / "Game.exe", securityRoot, modified) == nullptr &&
            Mentions(modified.str(), "FileModified") && Mentions(modified.str(), "Game.exe"),
        "A modified player executable was allowed to start");
    WriteTextFile(releaseRoot / "Game.exe", "player");
    signRelease(4U, true);
    std::ostringstream rolledBack;
    Require(kb::game::VerifyPackagedRelease(anchor.anchor, releaseRoot, releaseRoot / "Game.exe", securityRoot, rolledBack) == nullptr &&
            Mentions(rolledBack.str(), "older than a release"),
        "An older release started after a newer one had run");
    signRelease(3U, false);
    std::ostringstream unprotected;
    Require(kb::game::VerifyPackagedRelease(anchor.anchor, releaseRoot, releaseRoot / "Game.exe", securityRoot, unprotected) != nullptr,
        "A release without anti-rollback was refused for its release number");
    kb::security::InstallVerifiedRelease(nullptr);
}

// A game cooked with compressed blocks, split into a base and a chunk and patched, mounts as one
// pack set in the player, and its packaged release binds every pack and the pack set index: an
// older but correctly sealed patch in place of the shipped one, or an edited index, stops it.
void RunPackSetPackagingTests() {
    namespace bake = kb::assets::bake;
    const Fixture fixture = BuildFixture(TestRoot() / "pack_set_project", "Project");
    kb::project::ProjectSettings settings;
    settings.defaultMap = fixture.sceneVirtualPath;
    settings.physicsLayersAsset.clear();
    settings.inputEnabled = false;
    WriteSettings(fixture.root, settings);
    // A texture of world cell 0_0 the scene shows, so the cook carries it.
    const std::filesystem::path texturePath = fixture.root / "Assets/Cells/0_0/Ground.tga";
    std::string texture(18U + 4U * 4U * 3U, '\0');
    texture[2] = 2; texture[12] = 4; texture[14] = 4; texture[16] = 24;
    WriteTextFile(texturePath, texture);
    {
        kb::scene::Scene scene;
        Require(scene.Assets().Manager().RegisterLoader(std::make_unique<kb::render::RenderTextureAssetLoader>()),
            "Pack set fixture loader registration failed");
        Require(scene.Assets().MountProject(fixture.root), "Pack set fixture mount failed");
        static_cast<void>(scene.Assets().Discover());
        const auto* metadata = scene.Assets().Manager().Registry().FindByPath("/Game/Cells/0_0/Ground.tga");
        Require(metadata != nullptr, "Pack set fixture texture was not discovered");
        const auto object = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{ .name = "Ground" });
        scene.Components().UI().Set(object.Entity(), kb::scene::UIRawImage{ .imageAssetId = metadata->id.value });
        Require(kb::scene::SceneDocumentService::Save(scene, fixture.root / "Assets/Scenes/Main.21kbscene", "Main"),
            "Pack set fixture scene save failed");
    }
    const std::filesystem::path work = TestRoot() / "pack_set_work";
    const auto cook = [&](const std::filesystem::path& output) {
        std::ostringstream diagnostics;
        const auto cooked = kb::game::CookProject(
            kb::game::ProjectCookRequest{ .projectPath = fixture.root, .targetProfileId = "Windows.x64",
                .outputPackPath = output, .packCompressionLevel = 9 },
            diagnostics);
        Require(cooked.succeeded, cooked.error.c_str());
    };
    const std::filesystem::path firstCook = work / "first.kbpack";
    cook(firstCook);
    {
        bake::AssetPackReader reader;
        Require(reader.Mount(firstCook) == bake::AssetPackReadStatus::Success, "A compressed cook does not mount");
        bool compressed = false;
        for (const bake::AssetPackArtifactEntry& artifact : reader.Artifacts()) {
            for (const bake::AssetPackBlockEntry& block : artifact.blocks) {
                compressed = compressed || block.compression == bake::AssetPackBlockCompression::Zstd;
            }
        }
        Require(compressed, "A cook asked to compress stored no block compressed");
    }

    // Version 1 of the game: a base and world cell 0_0 split into a chunk.
    const std::filesystem::path release = TestRoot() / "pack_set_release";
    std::filesystem::create_directories(release);
    const std::vector<bake::AssetPackChunkRule> rules{
        { .label = "cell_0_0", .virtualPathPrefixes = { "/Game/Cells/0_0/" }, .output = release / "Game.cell_0_0.kbpack" } };
    bake::AssetPackSplitReport split{};
    std::string error;
    const bool splitOk = bake::SplitRuntimeAssetPack(
        firstCook, release / "Game.kbpack", rules, bake::AssetPackBlockCompression::Zstd, 9, split, error);
    Require(splitOk, error.c_str());
    // Version 2: the texture changed; the patch carries it.
    texture[18] = 127;
    WriteTextFile(texturePath, texture);
    const std::filesystem::path secondCook = work / "second.kbpack";
    cook(secondCook);
    WriteTextFile(release / "Game.kbpackset", "21kb-pack-set 1\nbase Game.kbpack\nchunk cell_0_0 Game.cell_0_0.kbpack\n");
    bake::AssetPackPatchReport patch{};
    const bool patched = bake::BuildAssetPackPatch(bake::AssetPackPatchRequest{ .current = release / "Game.kbpackset",
        .next = secondCook, .output = release / "Game.patch-0001.kbpack", .label = "patch-0001", .patchLevel = 1U },
        patch, error);
    Require(patched, error.c_str());
    Require(patch.changedAssets == 1U, "The patch does not carry exactly the changed texture");
    // An older build of the same patch, as an attacker would plant it.
    texture[18] = 64;
    WriteTextFile(texturePath, texture);
    const std::filesystem::path olderCook = work / "older.kbpack";
    cook(olderCook);
    const bool olderPatched = bake::BuildAssetPackPatch(bake::AssetPackPatchRequest{ .current = release / "Game.kbpackset",
        .next = olderCook, .output = work / "Game.patch-0001.kbpack", .label = "patch-0001", .patchLevel = 1U },
        patch, error);
    Require(olderPatched, error.c_str());
    const std::string index =
        "21kb-pack-set 1\nbase Game.kbpack\nchunk cell_0_0 Game.cell_0_0.kbpack\npatch 1 patch-0001 Game.patch-0001.kbpack\n";
    WriteTextFile(release / "Game.kbpackset", index);

    // A development player mounts the set beside the base pack.
    {
        kb::game::GameProjectRuntime runtime{};
        std::ostringstream diagnostics;
        Require(kb::game::ReadGameProjectRuntime(release, {}, runtime, diagnostics) && runtime.assetPack != nullptr,
            diagnostics.str().c_str());
        Require(runtime.assetPack->ContainerCount() == 3U, "The player did not mount the whole pack set");
        const bake::RuntimeAssetManifestEntry* ground = runtime.assetPack->FindAsset("/Game/Cells/0_0/Ground.tga");
        Require(ground != nullptr && runtime.assetPack->AssetContainer(ground->id) == 2U,
            "The patched texture is not answered by the patch");
    }

    // Sealed and signed into a release.
    kb::security::ReleaseSigningKey key;
    Require(kb::security::GenerateReleaseSigningKey(key), "Pack set release key could not be generated");
    for (const char* member : { "Game.kbpack", "Game.cell_0_0.kbpack", "Game.patch-0001.kbpack" }) {
        {
            const bool succeeded = bake::SealAssetPack(release / member, key, nullptr, error);
            Require(succeeded, error.c_str());
        }
    }
    {
        const bool succeeded = bake::SealAssetPack(work / "Game.patch-0001.kbpack", key, nullptr, error);
        Require(succeeded, error.c_str());
    }
    WriteTextFile(release / "Game.exe", "player");
    kb::security::TrustAnchor anchor{};
    anchor.productId = "Publisher.PackSet";
    anchor.releaseKey = key.publicKey;
    {
        kb::security::ReleaseManifest manifest{};
        manifest.productId = anchor.productId;
        manifest.contentVersion = "2.0.0";
        manifest.releaseNumber = 2U;
        {
            const bool succeeded = kb::security::BuildReleaseManifest(release, manifest, error);
            Require(succeeded, error.c_str());
        }
        WriteTextFile(release / "release.kbmanifest", kb::security::SignReleaseManifest(manifest, key));
    }
    const std::filesystem::path securityRoot = TestRoot() / "pack_set_security";
    std::ostringstream releaseErrors;
    const auto installed = kb::game::VerifyPackagedRelease(anchor, release, release / "Game.exe", securityRoot, releaseErrors);
    Require(installed != nullptr, releaseErrors.str().c_str());
    bake::AssetPackTrust trust{};
    trust.requiredSigner = key.publicKey;
    const auto mountSet = [&](bake::RuntimeAssetPack& pack) {
        return pack.MountSetIndex(release / "Game.kbpackset", bake::WindowsX64BakeTargetProfile(), bake::AssetPackAccess::Ranged,
            trust);
    };
    {
        bake::RuntimeAssetPack pack;
        Require(mountSet(pack) == bake::RuntimeAssetPackStatus::Success &&
                kb::game::PackBelongsToRelease(*installed, release / "Game.kbpack", pack, releaseErrors),
            "The release's own pack set was not bound to its manifest");
    }

    // The same release laid out for Linux: an ELF player without an extension beside the same
    // packs and index. The player verifies it the same way -- its own image hashed, the index
    // hashed, every pack bound by its seal -- and refuses a modified player, a planted shared
    // library and a missing manifest.
    {
        const std::filesystem::path linuxRoot = TestRoot() / "pack_set_release_linux";
        std::error_code linuxError;
        std::filesystem::remove_all(linuxRoot, linuxError);
        std::filesystem::create_directories(linuxRoot / "Licenses", linuxError);
        for (const char* member : { "Game.kbpack", "Game.cell_0_0.kbpack", "Game.patch-0001.kbpack", "Game.kbpackset" }) {
            std::filesystem::copy_file(release / member, linuxRoot / member, linuxError);
        }
        Require(!linuxError, "The Linux release fixture could not be staged");
        const std::string elf{ "\x7F" "ELF\x02\x01\x01 player image" };
        WriteTextFile(linuxRoot / "Game", elf);
        WriteTextFile(linuxRoot / "Licenses" / "notice.txt", "notices");
        const auto signLinux = [&] {
            std::filesystem::remove(linuxRoot / "release.kbmanifest", linuxError);
            kb::security::ReleaseManifest manifest{};
            manifest.productId = anchor.productId;
            manifest.contentVersion = "2.0.0";
            manifest.releaseNumber = 2U;
            {
                const bool succeeded = kb::security::BuildReleaseManifest(linuxRoot, manifest, error);
                Require(succeeded, error.c_str());
            }
            WriteTextFile(linuxRoot / "release.kbmanifest", kb::security::SignReleaseManifest(manifest, key));
        };
        signLinux();
        std::ostringstream linuxErrors;
        const auto linuxRelease = kb::game::VerifyPackagedRelease(anchor, linuxRoot, linuxRoot / "Game", securityRoot, linuxErrors);
        Require(linuxRelease != nullptr, linuxErrors.str().c_str());
        {
            bake::RuntimeAssetPack pack;
            Require(pack.MountSetIndex(linuxRoot / "Game.kbpackset", bake::WindowsX64BakeTargetProfile(),
                        bake::AssetPackAccess::Ranged, trust) == bake::RuntimeAssetPackStatus::Success &&
                    kb::game::PackBelongsToRelease(*linuxRelease, linuxRoot / "Game.kbpack", pack, linuxErrors),
                "A Linux release's pack set was not bound to its manifest");
        }
        WriteTextFile(linuxRoot / "Game", elf + "patched");
        std::ostringstream modified;
        Require(kb::game::VerifyPackagedRelease(anchor, linuxRoot, linuxRoot / "Game", securityRoot, modified) == nullptr &&
                Mentions(modified.str(), "Game"),
            "A modified Linux player was allowed to start");
        WriteTextFile(linuxRoot / "Game", elf);
        WriteTextFile(linuxRoot / "libplanted.so", "planted");
        std::ostringstream planted;
        Require(kb::game::VerifyPackagedRelease(anchor, linuxRoot, linuxRoot / "Game", securityRoot, planted) == nullptr &&
                Mentions(planted.str(), "UnlistedFile") && Mentions(planted.str(), "libplanted.so"),
            "A Linux release with a planted shared library was allowed to start");
        std::filesystem::remove(linuxRoot / "libplanted.so", linuxError);
        // A stray file without an extension -- an executable on Linux -- is refused like an unlisted
        // DLL on Windows, beside the player or deeper; a listed one is part of the release.
        for (const std::filesystem::path stray : { linuxRoot / "helper", linuxRoot / "Licenses" / "run-me" }) {
            WriteTextFile(stray, elf + "stray");
            std::ostringstream strayErrors;
            Require(kb::game::VerifyPackagedRelease(anchor, linuxRoot, linuxRoot / "Game", securityRoot, strayErrors) == nullptr &&
                    Mentions(strayErrors.str(), "UnlistedFile") &&
                    Mentions(strayErrors.str(), stray.lexically_relative(linuxRoot).generic_string()),
                "A Linux release with a stray file without an extension was allowed to start");
            std::filesystem::remove(stray, linuxError);
        }
        WriteTextFile(linuxRoot / "Licenses" / "COPYING", "license text without an extension");
        signLinux();
        std::ostringstream listedErrors;
        Require(kb::game::VerifyPackagedRelease(anchor, linuxRoot, linuxRoot / "Game", securityRoot, listedErrors) != nullptr,
            ("A Linux release whose extension-less files are all listed was refused: " + listedErrors.str()).c_str());
        std::filesystem::remove(linuxRoot / "release.kbmanifest", linuxError);
        std::ostringstream withoutManifest;
        Require(kb::game::VerifyPackagedRelease(anchor, linuxRoot, linuxRoot / "Game", securityRoot, withoutManifest) == nullptr,
            "A Linux release without its manifest was allowed to start");
        std::filesystem::remove_all(linuxRoot, linuxError);
    }
    // The older patch is sealed by the same key and mounts -- and is not this release's.
    std::filesystem::copy_file(work / "Game.patch-0001.kbpack", release / "Game.patch-0001.kbpack",
        std::filesystem::copy_options::overwrite_existing);
    {
        bake::RuntimeAssetPack pack;
        std::ostringstream refusal;
        Require(mountSet(pack) == bake::RuntimeAssetPackStatus::Success &&
                !kb::game::PackBelongsToRelease(*installed, release / "Game.kbpack", pack, refusal) &&
                Mentions(refusal.str(), "not the one this release shipped"),
            "A patch swapped for an older sealed build was bound to the release");
    }
    // The index is hashed at startup: dropping the patch from it stops the game.
    WriteTextFile(release / "Game.kbpackset", "21kb-pack-set 1\nbase Game.kbpack\nchunk cell_0_0 Game.cell_0_0.kbpack\n");
    std::ostringstream edited;
    Require(kb::game::VerifyPackagedRelease(anchor, release, release / "Game.exe", securityRoot, edited) == nullptr &&
            Mentions(edited.str(), "FileModified") && Mentions(edited.str(), "Game.kbpackset"),
        "An edited pack set index was allowed to start");
    kb::security::InstallVerifiedRelease(nullptr);
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

struct PartitionedWorldFixture {
    Fixture fixture;
    std::uint64_t worldId = 0U;
};

// A project whose default map places Forest.21kbworld: 64 m cells, three rocks with a mesh along
// X, a night-only lamp, HLOD proxies enabled.
[[nodiscard]] PartitionedWorldFixture BuildPartitionedWorldFixture(std::string_view name, std::uint32_t regionCells = 16U) {
    const Fixture fixture = BuildFixture(TestRoot() / name, "Project");
    WriteTextFile(fixture.root / "Assets" / "Meshes" / "Rock.obj",
        "v 0 0 0\nv 1 0 0\nv 1 1 0\nv 0 1 0\nvt 0 0\nvt 1 0\nvt 1 1\nvt 0 1\nvn 0 0 1\n"
        "usemtl stone\nf 1/1/1 2/2/1 3/3/1\nf 1/1/1 3/3/1 4/4/1\n");
    const std::uint64_t meshId = kb::assets::MakeAssetId(kb::assets::NormalizeAssetPath("/Game/Meshes/Rock.obj") + ":RenderMesh").value;
    const std::filesystem::path descriptorPath = fixture.root / "Assets" / "Worlds" / "Forest.21kbworld";
    kb::world::WorldDescriptor descriptor;
    descriptor.guid = "forest";
    descriptor.name = "Forest";
    descriptor.cellSize = 64.0;
    descriptor.objectsDirectory = "Forest.objects";
    descriptor.dataLayers = { { .name = "night", .initiallyActive = false } };
    descriptor.hlod = { .enabled = true, .range = 512.0, .triangleRatio = 0.5 };
    descriptor.regionCells = regionCells;
    // A ground collider under the first three cells gives every build navigation meshes to carry.
    descriptor.navigation.enabled = true;
    std::string error;
    Require(kb::world::WorldDescriptorIO::Write(descriptorPath, descriptor, error), "World fixture descriptor could not be written");
    const auto writeObject = [&](const std::string& name, double x, const std::string& layer) {
        kb::world::WorldObjectFile object;
        object.header.guid = kb::world::MakeDeterministicWorldObjectGuid(name);
        object.header.name = name;
        object.header.position = { x, 0.0, 10.0 };
        object.header.dataLayer = layer;
        object.header.nodeCount = 1U;
        kb::scene::ScenePrefabNodeDesc node;
        node.stableId = kb::world::WorldObjectStableId(object.header.guid, 0U);
        node.name = name;
        node.transform.localPosition = { static_cast<float>(x), 0.0F, 10.0F };
        node.components.meshRenderer = kb::scene::MeshRendererComponent{ .meshAssetId = meshId };
        static_cast<void>(object.prefab.AddNode(node));
        const std::vector<std::uint8_t> bytes = kb::world::WorldObjectFileIO::Serialize(object, error);
        Require(!bytes.empty() && kb::world::WorldObjectFileIO::WriteBytes(
            descriptorPath.parent_path() / "Forest.objects" / (object.header.guid + ".21kbobject"), bytes, error),
            "World fixture object could not be written");
    };
    for (int index = 0; index < 3; ++index) {
        writeObject("Rock" + std::to_string(index), index * 64.0 + 10.0, {});
    }
    writeObject("Lamp", 12.0, "night");
    {
        kb::world::WorldObjectFile ground;
        ground.header.guid = kb::world::MakeDeterministicWorldObjectGuid("Ground");
        ground.header.name = "Ground";
        ground.header.position = { 96.0, -0.25, 10.0 };
        ground.header.nodeCount = 1U;
        kb::scene::ScenePrefabNodeDesc node;
        node.stableId = kb::world::WorldObjectStableId(ground.header.guid, 0U);
        node.name = "Ground";
        node.transform.localPosition = { 96.0F, -0.25F, 10.0F };
        node.components.collider = kb::scene::ColliderComponent{ .shape = kb::scene::ColliderShape::Box, .boxSize = { 192.0F, 0.5F, 20.0F } };
        static_cast<void>(ground.prefab.AddNode(node));
        const std::vector<std::uint8_t> bytes = kb::world::WorldObjectFileIO::Serialize(ground, error);
        Require(!bytes.empty() && kb::world::WorldObjectFileIO::WriteBytes(
            descriptorPath.parent_path() / "Forest.objects" / (ground.header.guid + ".21kbobject"), bytes, error),
            "World fixture ground could not be written");
    }
    std::uint64_t worldId = 0U;
    {
        kb::scene::Scene authored;
        Require(authored.Assets().MountProject(fixture.root), "World fixture project could not be mounted");
        static_cast<void>(authored.Assets().Discover());
        const kb::assets::AssetMetadata* world = authored.Assets().Manager().Registry().FindByPath("/Game/Worlds/Forest.21kbworld");
        Require(world != nullptr && world->type == "World", "World fixture descriptor was not discovered as a world");
        worldId = world->id.value;
        const kb::scene::SceneObject owner = authored.Entities().CreateObject(kb::scene::SceneObjectDesc{ .name = "Forest" });
        authored.Components().ContentInstances().Set(owner.Entity(), kb::scene::ContentInstanceComponent{
            .assetId = worldId, .kind = kb::scene::ContentInstanceKind::PartitionedWorld });
        Require(kb::scene::SceneDocumentService::Save(authored, fixture.root / "Assets" / "Scenes" / "Main.21kbscene", "Main"),
            "World fixture scene could not be saved");
    }
    kb::project::ProjectSettings settings;
    settings.defaultMap = fixture.sceneVirtualPath;
    settings.physicsLayersAsset.clear();
    settings.inputEnabled = false;
    WriteSettings(fixture.root, settings);
    return { .fixture = fixture, .worldId = worldId };
}

// Relative path -> bytes of every file below `root`.
[[nodiscard]] std::map<std::string, std::string> FileTree(const std::filesystem::path& root) {
    std::map<std::string, std::string> files;
    for (const std::filesystem::directory_entry& entry : std::filesystem::recursive_directory_iterator{ root }) {
        if (!entry.is_regular_file()) continue;
        std::ifstream input{ entry.path(), std::ios::binary };
        files.emplace(entry.path().lexically_relative(root).generic_string(),
            std::string{ std::istreambuf_iterator<char>{ input }, std::istreambuf_iterator<char>{} });
    }
    return files;
}

// kb_cli, kb_cooker and the editor run one world build (kb::render::BuildWorldWithHlod): the
// same project yields byte-identical cells, cell index and HLOD proxies from the command line
// and from a cook. The editor side is compared by the world partition headless scenario.
void RunWorldBuildAgreementTest() {
    const PartitionedWorldFixture world = BuildPartitionedWorldFixture("world_build_cli");
    const std::filesystem::path cookedProject = TestRoot() / "world_build_cook";
    std::filesystem::copy(world.fixture.root, cookedProject, std::filesystem::copy_options::recursive);

    const std::vector<std::string> arguments{ "build", "--project", world.fixture.root.string(),
        "--world", (world.fixture.root / "Assets" / "Worlds" / "Forest.21kbworld").string() };
    const kb::cli::ArgumentList parsed{ arguments };
    std::ostringstream output;
    Require(kb::cli::RunWorldCommand(parsed, kb::cli::CommandIo{ .out = output, .err = output }) == 0, output.str().c_str());
    Require(Mentions(output.str(), "built 4 cells and 3 HLOD proxies"), "kb_cli world build did not build the proxies");

    std::ostringstream diagnostics;
    const kb::game::ProjectCookResult cooked = kb::game::CookProject(
        kb::game::ProjectCookRequest{ .projectPath = cookedProject, .targetProfileId = "Windows.x64",
            .outputPackPath = TestRoot() / "world_build_package" / "Game.kbpack" },
        diagnostics);
    Require(cooked.succeeded, cooked.error.c_str());

    const std::map<std::string, std::string> fromCli = FileTree(world.fixture.root / "Assets" / "Worlds" / "Forest.cells");
    const std::map<std::string, std::string> fromCook = FileTree(cookedProject / "Assets" / "Worlds" / "Forest.cells");
    const std::size_t proxies = static_cast<std::size_t>(std::ranges::count_if(fromCli, [](const auto& file) {
        return file.first.ends_with(".obj") && Mentions(file.second, "usemtl slot0");
    }));
    Require(proxies == 3U, "kb_cli did not write the three HLOD proxies");
    const std::size_t navMeshes = static_cast<std::size_t>(std::ranges::count_if(fromCli, [](const auto& file) {
        return file.first.ends_with(".21kbnavmesh");
    }));
    Require(navMeshes == 3U, "kb_cli did not write a navigation mesh for each cell over the ground");
    Require(fromCli == fromCook, "kb_cli and kb_cooker built the same world differently");
}

// A partitioned world placed in the default map ships as its built cells: the cooker builds
// the world from its object files, follows world -> cell index -> cells and HLOD proxies,
// and the packaged runtime streams the cells without any loose file.
void RunPartitionedWorldCookTest() {
    namespace bake = kb::assets::bake;
    const PartitionedWorldFixture placed = BuildPartitionedWorldFixture("partitioned_world");
    const Fixture& fixture = placed.fixture;
    const std::uint64_t worldId = placed.worldId;
    const std::filesystem::path packPath = TestRoot() / "partitioned_world_package" / "Game.kbpack";
    std::ostringstream diagnostics;
    const kb::game::ProjectCookResult cooked = kb::game::CookProject(
        kb::game::ProjectCookRequest{ .projectPath = fixture.root, .targetProfileId = "Windows.x64", .outputPackPath = packPath },
        diagnostics);
    Require(cooked.succeeded, cooked.error.c_str());
    Require(Mentions(diagnostics.str(), "built 1 partitioned world(s): 4 cells, 3 HLOD proxies, 3 navigation meshes"),
        "The cooker did not build the partitioned world before collecting assets");

    auto pack = std::make_shared<bake::RuntimeAssetPack>();
    Require(pack->Mount(packPath, bake::WindowsX64BakeTargetProfile()) == bake::RuntimeAssetPackStatus::Success,
        "Partitioned world package could not be mounted");
    {
        kb::scene::Scene runtime;
        kb::assets::AssetManager& manager = runtime.Assets().Manager();
        Require(manager.MountRuntimePack(pack), "Partitioned world package registry mount failed");
        for (const char* path : { "/Game/Worlds/Forest.21kbworld", "/Game/Worlds/Forest.cells/Forest.21kbcells",
                 "/Game/Worlds/Forest.cells/base/r_0_0/c_0_0.21kbscene", "/Game/Worlds/Forest.cells/base/r_0_0/c_2_0.21kbscene",
                 "/Game/Worlds/Forest.cells/layer.night/r_0_0/c_0_0.21kbscene", "/Game/Worlds/Forest.cells/hlod/r_0_0/h_1_0.obj",
                 "/Game/Worlds/Forest.cells/nav/r_0_0/n_0_0.21kbnavmesh", "/Game/Worlds/Forest.cells/nav/r_0_0/n_2_0.21kbnavmesh" }) {
            Require(manager.Registry().FindByPath(path) != nullptr, (std::string{ "The package is missing " } + path).c_str());
        }
        Require(manager.Registry().FindByPath("/Game/Worlds/Forest.cells/hlod/r_0_0/h_1_0.obj")->type == "RenderMesh",
            "The HLOD proxy was not packaged as a mesh");
        const kb::scene::SceneObject owner = runtime.Entities().CreateObject(kb::scene::SceneObjectDesc{ .name = "Forest" });
        runtime.Components().ContentInstances().Set(owner.Entity(), kb::scene::ContentInstanceComponent{
            .assetId = worldId, .kind = kb::scene::ContentInstanceKind::PartitionedWorld });
        kb::world::WorldPartitionRuntime world{ runtime };
        static_cast<void>(world.AddSource({ .position = { 10.0, 0.0, 10.0 }, .loadRadius = 30.0, .unloadRadius = 40.0, .priority = 0 }));
        const auto deadline = Clock::now() + std::chrono::seconds{ 30 };
        while (world.CellState(owner.Entity(), { 0, 0 }) != kb::world::WorldCellState::Loaded && Clock::now() < deadline) {
            static_cast<void>(runtime.Runtime().Update(1.0F / 60.0F));
            std::this_thread::yield();
        }
        Require(world.CellState(owner.Entity(), { 0, 0 }) == kb::world::WorldCellState::Loaded,
            "The packaged world did not stream its first cell");
        Require(world.CellState(owner.Entity(), { 2, 0 }) == kb::world::WorldCellState::Unloaded &&
                world.CellState(owner.Entity(), { 0, 0 }, "night") == kb::world::WorldCellState::Unloaded,
            "The packaged world streamed cells outside the source or an inactive layer");
        // The cell's navigation tiles stream out of the package with it.
        while (!world.IsNavMeshLoaded(owner.Entity(), { 0, 0 }) && Clock::now() < deadline) {
            static_cast<void>(runtime.Runtime().Update(1.0F / 60.0F));
            std::this_thread::yield();
        }
        Require(world.IsNavMeshLoaded(owner.Entity(), { 0, 0 }) && !world.IsNavMeshLoaded(owner.Entity(), { 2, 0 }),
            "The packaged world did not stream the navigation tiles of its first cell");
        const kb::scene::NavPathResult path = runtime.Navigation().FindPath(kb::math::DVec3{ 3.0, 0.0, 4.0 }, kb::math::DVec3{ 50.0, 0.0, 4.0 });
        Require(path.status == kb::scene::NavPathStatus::Complete, "Agents cannot find a path over the packaged navigation tiles");
    }
    pack->Unmount();

    // A world region's cells and proxies, and a data layer's cells, split into chunk packs of
    // their own; the set streams them from there.
    const std::filesystem::path chunks = TestRoot() / "partitioned_world_chunks";
    std::error_code removeError;
    std::filesystem::remove_all(chunks, removeError);
    std::filesystem::create_directories(chunks);
    std::string error;
    bake::AssetPackWorldRegion east{};
    bake::AssetPackWorldRegion night{};
    Require(bake::ParseAssetPackWorldRegion("/Game/Worlds/Forest.21kbworld@1:0..2:0", east, error) &&
            bake::ParseAssetPackWorldRegion("/Game/Worlds/Forest.21kbworld#night", night, error),
        error.c_str());
    const std::vector<bake::AssetPackChunkRule> rules{
        { .label = "forest_east", .worldRegions = { east }, .output = chunks / "Game.forest_east.kbpack" },
        { .label = "forest_night", .worldRegions = { night }, .output = chunks / "Game.forest_night.kbpack" },
    };
    bake::AssetPackSplitReport split{};
    {
        const bool succeeded = bake::SplitRuntimeAssetPack(
            packPath, chunks / "Game.kbpack", rules, bake::AssetPackBlockCompression::Zstd, 9, split, error);
        Require(succeeded, error.c_str());
    }
    Require(split.chunkAssets == std::vector<std::uint64_t>{ 4U, 1U },
        "A world region did not take exactly its cells' scenes and proxies, or a layer its cells");
    const std::vector<bake::RuntimeAssetPackMount> mounts{
        { .path = chunks / "Game.kbpack" },
        { .path = rules[0].output, .role = bake::AssetPackRole::Chunk, .label = "forest_east" },
        { .path = rules[1].output, .role = bake::AssetPackRole::Chunk, .label = "forest_night" },
    };
    auto set = std::make_shared<bake::RuntimeAssetPack>();
    Require(set->MountSet(mounts, bake::WindowsX64BakeTargetProfile()) == bake::RuntimeAssetPackStatus::Success,
        "The split world did not mount as a pack set");
    const auto containerOf = [&](std::string_view path) {
        const bake::RuntimeAssetManifestEntry* entry = set->FindAsset(path);
        Require(entry != nullptr, (std::string{ "The split world lost " } + std::string{ path }).c_str());
        return set->AssetContainer(entry->id).value_or(99U);
    };
    Require(containerOf("/Game/Worlds/Forest.cells/base/r_0_0/c_0_0.21kbscene") == 0U &&
            containerOf("/Game/Worlds/Forest.cells/base/r_0_0/c_1_0.21kbscene") == 1U &&
            containerOf("/Game/Worlds/Forest.cells/base/r_0_0/c_2_0.21kbscene") == 1U &&
            containerOf("/Game/Worlds/Forest.cells/hlod/r_0_0/h_1_0.obj") == 1U &&
            containerOf("/Game/Worlds/Forest.cells/hlod/r_0_0/h_0_0.obj") == 0U &&
            containerOf("/Game/Worlds/Forest.cells/layer.night/r_0_0/c_0_0.21kbscene") == 2U &&
            containerOf("/Game/Worlds/Forest.cells/Forest.21kbcells") == 0U &&
            containerOf("/Game/Meshes/Rock.obj") == 0U,
        "The split put a world asset into the wrong pack");
    {
        kb::scene::Scene runtime;
        Require(runtime.Assets().Manager().MountRuntimePack(set), "The split world registry mount failed");
        const kb::scene::SceneObject owner = runtime.Entities().CreateObject(kb::scene::SceneObjectDesc{ .name = "Forest" });
        runtime.Components().ContentInstances().Set(owner.Entity(), kb::scene::ContentInstanceComponent{
            .assetId = worldId, .kind = kb::scene::ContentInstanceKind::PartitionedWorld });
        kb::world::WorldPartitionRuntime world{ runtime };
        world.SetDataLayerActive("night", true);
        static_cast<void>(world.AddSource({ .position = { 140.0, 0.0, 10.0 }, .loadRadius = 30.0, .unloadRadius = 40.0, .priority = 0 }));
        static_cast<void>(world.AddSource({ .position = { 10.0, 0.0, 10.0 }, .loadRadius = 30.0, .unloadRadius = 40.0, .priority = 0 }));
        const auto loaded = [&] {
            return world.CellState(owner.Entity(), { 2, 0 }) == kb::world::WorldCellState::Loaded &&
                world.CellState(owner.Entity(), { 0, 0 }, "night") == kb::world::WorldCellState::Loaded;
        };
        const auto deadline = Clock::now() + std::chrono::seconds{ 30 };
        while (!loaded() && Clock::now() < deadline) {
            static_cast<void>(runtime.Runtime().Update(1.0F / 60.0F));
            std::this_thread::yield();
        }
        Require(loaded(), "The pack set did not stream world cells from their chunk packs");
    }
    set->Unmount();
    bake::AssetPackWorldRegion missing{};
    Require(bake::ParseAssetPackWorldRegion("/Game/Worlds/Nowhere.21kbworld", missing, error), error.c_str());
    const std::vector<bake::AssetPackChunkRule> nowhere{
        { .label = "nowhere", .worldRegions = { missing }, .output = chunks / "Game.nowhere.kbpack" } };
    Require(!bake::SplitRuntimeAssetPack(packPath, chunks / "Other.kbpack", nowhere, bake::AssetPackBlockCompression::None, 9,
                split, error) &&
            Mentions(error, "no built partitioned world"),
        "A region of a world the pack does not hold was split");
}

// A big world need not ship as one pack: every region goes to a chunk pack of its own (the rules
// kb_cli world chunks prints and package_game.py --pack-chunk-world-regions applies), and the
// packaged runtime mounts the pack set and streams each cell out of its region's chunk.
void RunChunkedWorldPackageTest() {
    namespace bake = kb::assets::bake;
    const PartitionedWorldFixture world = BuildPartitionedWorldFixture("chunked_world", 1U);
    const std::filesystem::path work = TestRoot() / "chunked_world_work";
    std::ostringstream diagnostics;
    const kb::game::ProjectCookResult cooked = kb::game::CookProject(
        kb::game::ProjectCookRequest{ .projectPath = world.fixture.root, .targetProfileId = "Windows.x64",
            .outputPackPath = work / "Game.kbpack", .packCompressionLevel = 9 },
        diagnostics);
    Require(cooked.succeeded, cooked.error.c_str());

    const kb::world::WorldRegionChunksResult regions = kb::world::CollectWorldRegionChunks(world.fixture.root / "Assets", {});
    Require(regions.succeeded && regions.chunks.size() == 3U, "every region of the world must become a chunk");
    const std::filesystem::path release = TestRoot() / "chunked_world_release";
    std::filesystem::create_directories(release);
    std::vector<bake::AssetPackChunkRule> rules;
    std::string index = "21kb-pack-set 1\nbase Game.kbpack\n";
    for (const kb::world::WorldRegionChunk& chunk : regions.chunks) {
        const std::string file = "Game." + chunk.label + ".kbpack";
        rules.push_back({ .label = chunk.label, .virtualPathPrefixes = chunk.prefixes, .output = release / file });
        index += "chunk " + chunk.label + " " + file + "\n";
    }
    bake::AssetPackSplitReport split{};
    std::string error;
    const bool splitOk = bake::SplitRuntimeAssetPack(
        work / "Game.kbpack", release / "Game.kbpack", rules, bake::AssetPackBlockCompression::Zstd, 9, split, error);
    Require(splitOk, error.c_str());
    WriteTextFile(release / "Game.kbpackset", index);

    auto pack = std::make_shared<bake::RuntimeAssetPack>();
    Require(pack->MountSetIndex(release / "Game.kbpackset", bake::WindowsX64BakeTargetProfile()) == bake::RuntimeAssetPackStatus::Success &&
            pack->ContainerCount() == 4U,
        "The chunked world pack set does not mount");
    const auto containerOf = [&](const char* path) {
        const bake::RuntimeAssetManifestEntry* entry = pack->FindAsset(std::string_view{ path });
        return entry == nullptr ? std::optional<std::uint32_t>{} : pack->AssetContainer(entry->id);
    };
    const std::optional<std::uint32_t> first = containerOf("/Game/Worlds/Forest.cells/base/r_0_0/c_0_0.21kbscene");
    const std::optional<std::uint32_t> last = containerOf("/Game/Worlds/Forest.cells/base/r_2_0/c_2_0.21kbscene");
    Require(first.has_value() && last.has_value() && *first != 0U && *last != 0U && *first != *last &&
            containerOf("/Game/Worlds/Forest.cells/hlod/r_2_0/h_2_0.obj") == last &&
            containerOf("/Game/Worlds/Forest.cells/nav/r_2_0/n_2_0.21kbnavmesh") == last &&
            containerOf("/Game/Worlds/Forest.cells/layer.night/r_0_0/c_0_0.21kbscene") == first &&
            containerOf("/Game/Worlds/Forest.cells/Forest.21kbcells") == std::optional<std::uint32_t>{ 0U },
        "Each region's cells and proxy must live in that region's chunk, the index in the base");
    {
        kb::scene::Scene runtime;
        Require(runtime.Assets().Manager().MountRuntimePack(pack), "The chunked world registry mount failed");
        const kb::scene::SceneObject owner = runtime.Entities().CreateObject(kb::scene::SceneObjectDesc{ .name = "Forest" });
        runtime.Components().ContentInstances().Set(owner.Entity(), kb::scene::ContentInstanceComponent{
            .assetId = world.worldId, .kind = kb::scene::ContentInstanceKind::PartitionedWorld });
        kb::world::WorldPartitionRuntime partition{ runtime };
        const std::uint64_t source = partition.AddSource({ .position = { 10.0, 0.0, 10.0 }, .loadRadius = 30.0, .unloadRadius = 40.0, .priority = 0 });
        const auto streamUntil = [&](kb::world::WorldCellCoord cell, kb::world::WorldCellState state) {
            const auto deadline = Clock::now() + std::chrono::seconds{ 30 };
            while (partition.CellState(owner.Entity(), cell) != state && Clock::now() < deadline) {
                static_cast<void>(runtime.Runtime().Update(1.0F / 60.0F));
                std::this_thread::yield();
            }
            return partition.CellState(owner.Entity(), cell) == state;
        };
        Require(streamUntil({ 0, 0 }, kb::world::WorldCellState::Loaded), "A cell did not stream from its region's chunk pack");
        Require(partition.UpdateSource(source, { .position = { 140.0, 0.0, 10.0 }, .loadRadius = 30.0, .unloadRadius = 40.0, .priority = 0 }),
            "The source could not move");
        Require(streamUntil({ 2, 0 }, kb::world::WorldCellState::Loaded) && streamUntil({ 0, 0 }, kb::world::WorldCellState::Unloaded),
            "Cells of another region's chunk did not stream in and out");
        Require(partition.Worlds().front().lastFailure.empty(), "A chunked cell failed to load");
    }
    pack->Unmount();
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

    if (argc == 2 && std::string_view{ argv[1] } == "--texture-reuse") {
        RunTextureReuseCookTests();
        std::fputs("kb_game_core texture reuse tests passed\n", stdout);
        return EXIT_SUCCESS;
    }

    if (argc == 2 && std::string_view{ argv[1] } == "--windows-runtime-modules") {
        RunPackagedRuntimeModuleContractTests();
        RunWindowsRuntimeModulePackagingTests();
        std::fputs("kb_game_core Windows runtime-module tests passed\n", stdout);
        return EXIT_SUCCESS;
    }
    if (argc == 2 && std::string_view{ argv[1] } == "--packaged-trust") {
        RunPackagedTrustTests();
        RunPackSetPackagingTests();
        std::fputs("kb_game_core packaged trust tests passed\n", stdout);
        return EXIT_SUCCESS;
    }
    if (argc == 2 && std::string_view{ argv[1] } == "--partitioned-world") {
        RunWorldBuildAgreementTest();
        RunPartitionedWorldCookTest();
        RunChunkedWorldPackageTest();
        std::fputs("kb_game_core partitioned world package tests passed\n", stdout);
        return EXIT_SUCCESS;
    }
    if (argc == 2 && std::string_view{ argv[1] } == "--native-behaviours") {
        RunNativeBehaviourPackagingTests();
        std::fputs("kb_game_core native-behaviour package tests passed\n", stdout);
        return EXIT_SUCCESS;
    }
    Require(argc == 1, "kb_game_core tests received an unsupported argument");

    RunRuntimeDeltaTests();
    RunPackagedRuntimeModuleContractTests();
    RunCookOutputLockTest();
    RunTextureReuseCookTests();
    RunDeepOutputPathCookTest();
    RunWindowsRuntimeModulePackagingTests();
    RunNativeBehaviourPackagingTests();
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
