#pragma once

#include "engine/project/ProjectDescriptor.hpp"

#include <chrono>
#include <cstddef>
#include <filesystem>
#include <iosfwd>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace kb::scene {
class Scene;
}

namespace kb::game {

// Everything a shipped runtime host needs to bring a project up: the descriptor
// the engine module host is constructed from, and the values the game reads out
// of the project's own settings file.
struct GameProjectRuntime {
    kb::project::ProjectDescriptor descriptor{};
    std::filesystem::path projectRoot;
    // The name the finished game ships under; the window is titled with it.
    std::string gameName;
    std::string sceneReference;
    std::string physicsLayersAsset;
    std::string inputMappingContext;
    bool inputEnabled = true;
    std::vector<std::string> requiredModules;
};

// Directory holding the running executable. A packaged game keeps its project
// beside the binary, so this is also where the runtime looks when no project
// path was given on the command line.
[[nodiscard]] std::filesystem::path ExecutableDirectory();

// A frame that took longer than this is stepped as if it had taken exactly this
// long, so a stall cannot tunnel a moving body through the world.
inline constexpr float kMaximumRuntimeDeltaSeconds = 1.0F / 15.0F;

// The step the scene runtime is given for a frame that ran between the two
// instants. Never negative and never above the ceiling above, so neither a
// clock that appears to go backwards nor a stall reaches the simulation.
[[nodiscard]] float RuntimeDeltaSeconds(
    std::chrono::steady_clock::time_point previous,
    std::chrono::steady_clock::time_point current) noexcept;

// std::filesystem::path::string() THROWS when the text holds a character the
// process code page cannot spell, and a windowed process that lets an exception
// escape ends in abort() behind a modal dialog nobody can dismiss. Both of these
// are total: TryNarrow reports the loss instead of hiding it, so a caller that
// needs an exact name (a plugin binary, a cache directory) can decline rather
// than build a wrong one, and NarrowForDiagnostics always yields something
// printable so an error message can never be the thing that kills the game.
[[nodiscard]] std::optional<std::string> TryNarrow(std::wstring_view text);
[[nodiscard]] std::string NarrowForDiagnostics(std::wstring_view text);
[[nodiscard]] std::string NarrowForDiagnostics(const std::filesystem::path& path);

// Reads <project>/Project.21kbproject plus <project>/Config/ProjectSettings.ini.
// `sceneOverride` wins over ProjectSettings::defaultMap when it is not empty.
[[nodiscard]] bool ReadGameProjectRuntime(
    const std::filesystem::path& projectPath,
    std::string_view sceneOverride,
    GameProjectRuntime& runtime,
    std::ostream& err);

// Registers the renderer-owned asset loaders every runtime host needs before it
// mounts a project.
[[nodiscard]] bool RegisterGameAssetLoaders(kb::scene::Scene& scene, std::ostream& err);

// Mounts the project, discovers its assets, applies its physics layers and
// input mapping, and loads the configured scene. `loadedScenePath` receives the
// physical scene file that was read and `discoveredAssets` the discovery count.
[[nodiscard]] bool LoadGameProjectScene(
    const GameProjectRuntime& runtime,
    kb::scene::Scene& scene,
    std::filesystem::path& loadedScenePath,
    std::size_t& discoveredAssets,
    std::ostream& err);

} // namespace kb::game
