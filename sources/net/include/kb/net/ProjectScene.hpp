#pragma once

#include "engine/scene/Scene.hpp"
#include "engine/network/ReplicationDeclarations.hpp"

#include <filesystem>
#include <memory>
#include <string>

namespace kb::net {

// A project opened the way a server or a headless client runs it: no window, no renderer. The scene is
// created and the project's assets are mounted; the scene file is found but not loaded, so the caller
// attaches the script host and the network runtime before the scripts start.
struct OpenedProjectScene {
    std::unique_ptr<kb::scene::Scene> scene;
    std::filesystem::path projectRoot;
    std::filesystem::path scenePath;
    kb::network::ReplicationDeclarations replication;
};

// `sceneOption` is a /Game/ path, a path in the project or empty (the project's startup scene). False with
// `error` set when the project, its modules, its assets or the scene cannot be found.
[[nodiscard]] bool OpenProjectScene(const std::filesystem::path& project, const std::string& sceneOption, OpenedProjectScene& opened, std::string& error);
[[nodiscard]] bool LoadReplicationDeclarations(const kb::scene::Scene& scene, kb::network::ReplicationDeclarations& declarations, std::string& error);

} // namespace kb::net
