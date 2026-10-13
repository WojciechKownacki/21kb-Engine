#include "CliCommands.hpp"

#include "engine/assets/AssetFileName.hpp"
#include "engine/project/ProjectManager.hpp"
#include "engine/project/ProjectSettings.hpp"
#include "engine/scene/PhysicsLayersAsset.hpp"
#include "engine/scene/PhysicsLayersAssetIO.hpp"

#include <charconv>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace kb::cli {

namespace {

constexpr std::string_view kDefaultLayersAsset = "/Game/Settings/PhysicsLayers.21kbphysicslayers";

// A layer by index (0-30) or by its name in the asset.
[[nodiscard]] std::optional<std::uint32_t> LayerIndex(const kb::scene::PhysicsLayersAsset& asset, std::string_view text) {
    std::uint32_t index = 0U;
    const auto [end, code] = std::from_chars(text.data(), text.data() + text.size(), index);
    if (code == std::errc{} && end == text.data() + text.size()) {
        return index < kb::scene::kPhysicsLayerCount ? std::optional{ index } : std::nullopt;
    }
    const int named = asset.LayerIndex(text);
    return named >= 0 ? std::optional{ static_cast<std::uint32_t>(named) } : std::nullopt;
}

} // namespace

// The project's layer collision matrix (Unity's Layer Collision Matrix): the asset named by
// ProjectSettings [Physics] LayersAsset, created at kDefaultLayersAsset when the project has none.
int RunPhysicsLayersCommand(const ArgumentList& arguments, CommandIo io) {
    const std::optional<std::string> project = arguments.Option("--project");
    if (!project.has_value()) {
        io.err << "error: physics-layers requires --project <dir>\n";
        return 1;
    }
    std::string error;
    const std::filesystem::path projectFile = kb::project::ProjectManager::FindProjectFile(kb::assets::Utf8Path(*project), error);
    if (projectFile.empty()) {
        io.err << "error: " << error << '\n';
        return 1;
    }
    const std::filesystem::path projectRoot = projectFile.parent_path();
    const std::filesystem::path settingsPath = kb::project::ProjectSettingsStore::FilePath(projectRoot);
    kb::project::ProjectSettings settings = kb::project::ProjectSettingsStore::Load(settingsPath).settings;
    const std::string virtualPath = settings.physicsLayersAsset.empty() ? std::string{ kDefaultLayersAsset } : settings.physicsLayersAsset;
    if (!virtualPath.starts_with("/Game/")) {
        io.err << "error: LayersAsset '" << virtualPath << "' is not a /Game/ path\n";
        return 1;
    }
    const std::filesystem::path file = projectRoot / "Assets" / kb::assets::Utf8Path(virtualPath.substr(6U));
    kb::scene::PhysicsLayersAsset asset;
    if (std::filesystem::exists(file)) {
        kb::scene::PhysicsLayersAssetLoadResult loaded = kb::scene::ReadPhysicsLayersAsset(file);
        if (!loaded.succeeded) {
            io.err << "error: " << virtualPath << ": " << loaded.error << '\n';
            return 1;
        }
        asset = std::move(loaded.asset);
    }

    for (const std::string& naming : arguments.Options("--name")) {
        const std::size_t equals = naming.find('=');
        const std::optional<std::uint32_t> index = equals == std::string::npos ? std::nullopt : LayerIndex(asset, naming.substr(0U, equals));
        if (!index.has_value() || equals + 1U == naming.size()) {
            io.err << "error: --name expects <layer index>=<Name>, got '" << naming << "'\n";
            return 1;
        }
        asset.layerNames[*index] = naming.substr(equals + 1U);
    }
    const auto pairs = [&](std::string_view option, bool interact) {
        for (const std::string& pair : arguments.Options(option)) {
            const std::size_t comma = pair.find(',');
            const std::optional<std::uint32_t> first = comma == std::string::npos ? std::nullopt : LayerIndex(asset, pair.substr(0U, comma));
            const std::optional<std::uint32_t> second = comma == std::string::npos ? std::nullopt : LayerIndex(asset, pair.substr(comma + 1U));
            if (!first.has_value() || !second.has_value()) {
                io.err << "error: " << option << " expects <layer>,<layer> (index 0-30 or name), got '" << pair << "'\n";
                return false;
            }
            asset.SetLayersInteract(*first, *second, interact);
        }
        return true;
    };
    if (!pairs("--ignore", false) || !pairs("--collide", true)) {
        return 1;
    }

    const bool changed = !arguments.Options("--name").empty() || !arguments.Options("--ignore").empty() || !arguments.Options("--collide").empty();
    if (changed) {
        std::error_code directoryError;
        std::filesystem::create_directories(file.parent_path(), directoryError);
        if (directoryError || !kb::scene::WritePhysicsLayersAsset(file, asset)) {
            io.err << "error: the physics layers asset could not be written: " << virtualPath << '\n';
            return 1;
        }
        if (settings.physicsLayersAsset != virtualPath) {
            settings.physicsLayersAsset = virtualPath;
            if (!kb::project::ProjectSettingsStore::Save(settingsPath, settings, error)) {
                io.err << "error: " << error << '\n';
                return 1;
            }
        }
    }
    io.out << "Physics layers: " << virtualPath << '\n';
    for (std::uint32_t index = 0U; index < kb::scene::kPhysicsLayerCount; ++index) {
        if (!asset.layerNames[index].empty()) io.out << "  " << index << ' ' << asset.layerNames[index] << '\n';
    }
    for (std::uint32_t first = 0U; first < kb::scene::kPhysicsLayerCount; ++first) {
        for (std::uint32_t second = first; second < kb::scene::kPhysicsLayerCount; ++second) {
            if (!asset.LayersInteract(first, second)) io.out << "  ignore " << first << ',' << second << '\n';
        }
    }
    return 0;
}

} // namespace kb::cli
