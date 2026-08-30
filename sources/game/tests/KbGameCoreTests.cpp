// The project bootstrap kb_game and kb_standalone_player share. It was lifted
// out of the sample as 239 lines of working code, and the only check it had -
// launching the real executable against a project with no plugins, no physics
// layers and no input mapping - never reached most of it: deleting the plugin
// path rewrite, the required-module check, the legacy-settings fallback, the
// input activation and the mapping context left every test in the repository
// green. These run the bootstrap directly, on projects that have those things.

#include "GameProjectRuntime.hpp"

#include "engine/assets/AssetManager.hpp"
#include "engine/assets/AssetMetadata.hpp"
#include "engine/assets/AssetRegistry.hpp"
#include "engine/input/InputAssetIO.hpp"
#include "engine/input/InputMappingContextAsset.hpp"
#include "engine/input/InputSubsystem.hpp"
#include "engine/project/ProjectManager.hpp"
#include "engine/project/ProjectSettings.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneAssets.hpp"
#include "engine/scene/SceneDocumentService.hpp"
#include "engine/scene/SceneEntities.hpp"
#include "engine/scene/SceneObject.hpp"
#include "engine/scene/SceneObjectDesc.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <system_error>
#include <vector>

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
    return std::filesystem::temp_directory_path() / "21kb_engine_kb_game_core_tests";
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
    std::filesystem::remove_all(root, error);
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

int main() {
    std::error_code error;
    std::filesystem::remove_all(TestRoot(), error);
    std::filesystem::create_directories(TestRoot(), error);
    Require(!error, "kb_game_core test root could not be prepared");

    RunRuntimeDeltaTests();
    RunNarrowingTests();
    RunSettingsTests();
    RunLegacySettingsFallbackTests();
    RunPluginTests();
    RunMissingProjectTests();
    RunSceneLoadTests();

    std::fputs("kb_game_core tests passed\n", stdout);
    return EXIT_SUCCESS;
}
