#pragma once

#include "engine/project/ProjectDescriptor.hpp"

#include <cstddef>
#include <filesystem>
#include <iosfwd>
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
