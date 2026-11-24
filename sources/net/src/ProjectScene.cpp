#include "kb/net/ProjectScene.hpp"

#include "engine/project/ProjectManager.hpp"
#include "engine/project/ProjectSettings.hpp"
#include "engine/scene/SceneAssets.hpp"
#include "engine/scene/SceneRuntime.hpp"
#include "engine/assets/bake/RuntimeAssetPack.hpp"
#include "kb/net/SceneReplication.hpp"

#include <fstream>
#include <iterator>

namespace kb::net {

bool LoadReplicationDeclarations(const kb::scene::Scene& scene, kb::network::ReplicationDeclarations& declarations, std::string& error) {
    declarations = {};
    error.clear();
    const auto& manager = scene.Assets().Manager();
    std::string text;
    if (manager.IsRuntimePackMounted()) {
        const auto pack = manager.RuntimePack();
        constexpr std::string_view path = "/Game/Config/Replication.json";
        bool present = false;
        for (const auto& file : pack->Manifest().auxiliaryFiles) present |= file.virtualPath == path;
        if (!present) return true;
        std::vector<std::uint8_t> bytes;
        if (pack->ReadAuxiliaryFile(path, bytes) != kb::assets::bake::RuntimeAssetPackStatus::Success) {
            error = "Replication.json: packaged declarations could not be read";
            return false;
        }
        text.assign(bytes.begin(), bytes.end());
    } else {
        std::filesystem::path path;
        for (const auto& mount : manager.Mounts().Mounts()) if (mount.name == "Game") path = mount.root.parent_path() / "Config" / "Replication.json";
        if (path.empty()) return true;
        std::error_code status;
        const bool present = std::filesystem::exists(path, status);
        if (status) { error = "Replication.json: declarations could not be inspected"; return false; }
        if (!present) return true;
        std::ifstream input(path, std::ios::binary);
        if (!input) { error = "Replication.json: declarations could not be read"; return false; }
        text.assign(std::istreambuf_iterator<char>(input), {});
        if (input.bad()) { error = "Replication.json: declarations could not be read"; return false; }
    }
    if (!kb::network::ParseReplicationDeclarations(text, declarations, error)) return false;
    return BuildReplicationRegistry(scene.Runtime().EcsWorld(), declarations, 1000.0F, error) != nullptr;
}

bool OpenProjectScene(const std::filesystem::path& project, const std::string& sceneOption, OpenedProjectScene& opened, std::string& error) {
    std::error_code pathError;
    const std::filesystem::path projectRoot = std::filesystem::absolute(project, pathError).lexically_normal();
    if (pathError || !std::filesystem::is_directory(projectRoot, pathError)) {
        error = "the project directory was not found: " + project.generic_string();
        return false;
    }
    std::string projectError;
    const std::filesystem::path projectFile = kb::project::ProjectManager::FindProjectFile(projectRoot, projectError);
    if (projectFile.empty()) {
        error = projectError;
        return false;
    }
    kb::project::ProjectDescriptorReadResult loaded = kb::project::ProjectManager::LoadProject(projectFile);
    if (!loaded.succeeded) {
        error = "the project descriptor could not be loaded: " + loaded.error;
        return false;
    }
    for (kb::project::ProjectPluginReference& plugin : loaded.descriptor.plugins) {
        const std::filesystem::path configured{ plugin.binaryPath };
        if (!plugin.enabled || configured.empty() || configured.is_absolute()) {
            continue;
        }
        std::error_code localError;
        if (std::filesystem::is_regular_file(projectRoot / configured, localError) && !localError) {
            plugin.binaryPath = (projectRoot / configured).string();
        }
    }
    auto scene = std::make_unique<kb::scene::Scene>(std::move(loaded.descriptor));
    if (!scene->ModuleDiagnostics().empty()) {
        error = "a project module did not load: " + scene->ModuleDiagnostics().front();
        return false;
    }
    if (!scene->Assets().MountProject(projectRoot)) {
        error = "the project assets could not be mounted under " + projectRoot.generic_string();
        return false;
    }
    static_cast<void>(scene->Assets().Discover());
    if (!LoadReplicationDeclarations(*scene, opened.replication, error)) return false;

    std::string name = sceneOption;
    if (name.empty()) {
        name = kb::project::ProjectSettingsStore::Load(kb::project::ProjectSettingsStore::FilePath(projectRoot)).settings.defaultMap;
    }
    if (name.empty()) {
        error = "the project has no startup scene; pass --scene <path>";
        return false;
    }
    std::filesystem::path scenePath;
    if (scene->Assets().Manager().Mounts().Resolve(name).has_value()) {
        const kb::assets::AssetMetadata* metadata = scene->Assets().Manager().Registry().FindByPath(name);
        if (metadata == nullptr) {
            error = "the scene asset was not found: " + name;
            return false;
        }
        scenePath = metadata->physicalPath;
    } else {
        scenePath = std::filesystem::path{ name };
        if (scenePath.is_relative()) {
            scenePath = projectRoot / scenePath;
        }
    }
    opened.scene = std::move(scene);
    opened.projectRoot = projectRoot;
    opened.scenePath = std::move(scenePath);
    return true;
}

} // namespace kb::net
