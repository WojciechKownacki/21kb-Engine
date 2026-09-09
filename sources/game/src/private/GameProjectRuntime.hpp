#pragma once

#include "engine/project/ProjectDescriptor.hpp"

#include <chrono>
#include <cstddef>
#include <filesystem>
#include <iosfwd>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace kb::scene {
class Scene;
}

namespace kb::assets::bake {
class RuntimeAssetPack;
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
    std::shared_ptr<kb::assets::bake::RuntimeAssetPack> assetPack;

    [[nodiscard]] bool IsPackaged() const noexcept {
        return assetPack != nullptr;
    }
};

// A frame that took longer than this is stepped as if it had taken exactly this
// long, so a stall cannot tunnel a moving body through the world.
inline constexpr float kMaximumRuntimeDeltaSeconds = 1.0F / 15.0F;

// The step the scene runtime is given for a frame that ran between the two
// instants. Never negative and never above the ceiling above, so neither a
// clock that appears to go backwards nor a stall reaches the simulation.
[[nodiscard]] float RuntimeDeltaSeconds(
    std::chrono::steady_clock::time_point previous,
    std::chrono::steady_clock::time_point current) noexcept;

// Advances the frame clock while a host is paused without advancing the
// simulation. The first frame after resume then measures only active time.
inline void ResetRuntimeDeltaOrigin(
    std::chrono::steady_clock::time_point& previous,
    std::chrono::steady_clock::time_point current) noexcept {
    previous = current;
}

// Windows-only conversions for APIs and command lines that use UTF-16. They are
// total: TryNarrow reports a lossy process-code-page conversion, while the
// diagnostic variant always returns printable text.
#if defined(_WIN32)
[[nodiscard]] std::optional<std::string> TryNarrow(std::wstring_view text);
[[nodiscard]] std::string NarrowForDiagnostics(std::wstring_view text);
#endif

// Directory holding the running Windows executable. Other runtime hosts receive
// their packaged storage roots from the platform lifecycle instead.
#if defined(_WIN32)
[[nodiscard]] std::filesystem::path ExecutableDirectory();
#endif

// Converts a native path for logs without changing the path used for I/O.
[[nodiscard]] std::string NarrowForDiagnostics(const std::filesystem::path& path);

// Reads <project>/Project.21kbproject plus <project>/Config/ProjectSettings.ini.
// `sceneOverride` wins over ProjectSettings::defaultMap when it is not empty.
[[nodiscard]] bool ReadGameProjectRuntime(
    const std::filesystem::path& projectPath,
    std::string_view sceneOverride,
    GameProjectRuntime& runtime,
    std::ostream& err);

// Platform package hosts may already own a zero-copy/memory-mapped pack (Android APK assets,
// browser fetch buffers). Resolve the same project runtime without extracting or remounting it
// through a filesystem path.
[[nodiscard]] bool ReadMountedGameProjectRuntime(
    std::shared_ptr<kb::assets::bake::RuntimeAssetPack> pack,
    std::filesystem::path projectRoot,
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
