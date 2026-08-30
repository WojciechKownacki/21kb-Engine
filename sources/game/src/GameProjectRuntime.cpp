#include "GameProjectRuntime.hpp"

#include "engine/assets/AssetManager.hpp"
#include "engine/assets/AssetMetadata.hpp"
#include "engine/assets/AssetRegistry.hpp"
#include "engine/input/InputSubsystem.hpp"
#include "engine/project/ParticleProjectPolicy.hpp"
#include "engine/project/ProjectManager.hpp"
#include "engine/project/ProjectSettings.hpp"
#include "engine/scene/PhysicsBackend.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneAssets.hpp"
#include "engine/scene/SceneDocumentService.hpp"
#include "engine/scene/SceneInputActivation.hpp"
#include "kb/render/resources/PostProcessProfileAssetLoader.hpp"
#include "kb/render/resources/RenderMaterialAssetLoader.hpp"
#include "kb/render/resources/RenderMeshAssetLoader.hpp"
#include "kb/render/resources/RenderTextureAssetLoader.hpp"
#include "kb/render/runtime/RuntimeMaterialParameterValidation.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <memory>
#include <ostream>
#include <system_error>
#include <utility>

namespace kb::game {

std::filesystem::path ExecutableDirectory() {
    std::wstring buffer;
    buffer.resize(MAX_PATH);
    for (;;) {
        const DWORD written =
            GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (written == 0U) {
            return std::filesystem::current_path();
        }
        if (written < buffer.size() - 1U) {
            buffer.resize(written);
            return std::filesystem::path{ buffer }.parent_path();
        }
        buffer.resize(buffer.size() * 2U);
    }
}

bool ReadGameProjectRuntime(
    const std::filesystem::path& projectPath,
    std::string_view sceneOverride,
    GameProjectRuntime& runtime,
    std::ostream& err) {
    std::error_code pathError;
    const std::filesystem::path absoluteInput =
        std::filesystem::absolute(projectPath, pathError).lexically_normal();
    if (pathError) {
        err << "project path could not be resolved: " << projectPath.string() << '\n';
        return false;
    }

    std::filesystem::path projectFile = absoluteInput;
    if (std::filesystem::is_directory(absoluteInput, pathError) && !pathError) {
        projectFile /= "Project.21kbproject";
    }
    if (pathError || !std::filesystem::is_regular_file(projectFile, pathError) || pathError) {
        err << "project descriptor was not found: " << projectFile.string() << '\n';
        return false;
    }

    kb::project::ProjectDescriptorReadResult loaded =
        kb::project::ProjectManager::LoadProject(projectFile);
    if (!loaded.succeeded) {
        err << "project descriptor load failed: " << loaded.error << '\n';
        return false;
    }

    const kb::project::ParticleProjectPolicyResult particlePolicy =
        kb::project::ParticleProjectPolicy::Inspect(projectFile.parent_path(), loaded.descriptor);
    if (!particlePolicy.IsRunnable()) {
        err << particlePolicy.diagnostic << '\n';
        return false;
    }

    runtime.projectRoot = projectFile.parent_path();
    // A persisted descriptor stores portable plugin filenames. Prefer a
    // project-local binary when one is packaged beside the project; otherwise
    // leave the filename intact so the module loader resolves the current
    // build or install layout.
    for (kb::project::ProjectPluginReference& plugin : loaded.descriptor.plugins) {
        if (!plugin.enabled) {
            continue;
        }
        if (!plugin.name.empty()) {
            runtime.requiredModules.push_back(plugin.name);
        }
        const std::filesystem::path configuredPath{ plugin.binaryPath };
        if (configuredPath.empty() || configuredPath.is_absolute()) {
            continue;
        }
        const std::filesystem::path projectLocalPath = runtime.projectRoot / configuredPath;
        std::error_code pluginError;
        if (std::filesystem::is_regular_file(projectLocalPath, pluginError) && !pluginError) {
            plugin.binaryPath = projectLocalPath.string();
        }
    }

    // The game reads the same settings file the editor writes, so shipping a change
    // means editing one file rather than rebuilding the project descriptor.
    const kb::project::ProjectSettingsLoadResult settings =
        kb::project::ProjectSettingsStore::Load(
            kb::project::ProjectSettingsStore::FilePath(runtime.projectRoot));
    if (!settings.Succeeded()) {
        err << "project settings could not be read: " << settings.error << '\n';
        return false;
    }
    // A package built before the settings file existed still carries its settings in
    // the descriptor, so it keeps running rather than starting with no scene.
    const kb::project::ProjectSettings resolved = settings.found
        ? settings.settings
        : kb::project::ProjectSettingsStore::FromLegacy(loaded.legacySettings, projectFile);
    runtime.gameName = resolved.gameName.empty() ? resolved.name : resolved.gameName;
    runtime.sceneReference =
        sceneOverride.empty() ? resolved.defaultMap : std::string{ sceneOverride };
    runtime.physicsLayersAsset = resolved.physicsLayersAsset;
    runtime.inputMappingContext = resolved.inputMappingContext;
    runtime.inputEnabled = resolved.inputEnabled;
    runtime.descriptor = std::move(loaded.descriptor);
    return true;
}

bool RegisterGameAssetLoaders(kb::scene::Scene& scene, std::ostream& err) {
    kb::assets::AssetManager& manager = scene.Assets().Manager();
    if (!manager.RegisterLoader(std::make_unique<kb::render::RenderMeshAssetLoader>())) {
        err << "mesh loader registration failed\n";
        return false;
    }
    if (!manager.RegisterLoader(std::make_unique<kb::render::RenderMaterialAssetLoader>())) {
        err << "material loader registration failed\n";
        return false;
    }
    if (!manager.RegisterLoader(std::make_unique<kb::render::RenderTextureAssetLoader>())) {
        err << "texture loader registration failed\n";
        return false;
    }
    if (!manager.RegisterLoader(std::make_unique<kb::render::PostProcessProfileAssetLoader>())) {
        err << "post-process profile loader registration failed\n";
        return false;
    }
    kb::render::InstallRuntimeMaterialParameterValidation(scene);
    return true;
}

bool LoadGameProjectScene(
    const GameProjectRuntime& runtime,
    kb::scene::Scene& scene,
    std::filesystem::path& loadedScenePath,
    std::size_t& discoveredAssets,
    std::ostream& err) {
    if (!scene.ModuleDiagnostics().empty()) {
        for (const std::string& diagnostic : scene.ModuleDiagnostics()) {
            err << "module diagnostic: " << diagnostic << '\n';
        }
        return false;
    }
    for (const std::string& module : runtime.requiredModules) {
        if (!scene.IsModuleActive(module)) {
            err << "configured module is not active: " << module << '\n';
            return false;
        }
    }
    if (!RegisterGameAssetLoaders(scene, err)) {
        return false;
    }
    if (!scene.Assets().MountProject(runtime.projectRoot)) {
        err << "project assets could not be mounted: " << runtime.projectRoot.string() << '\n';
        return false;
    }
    discoveredAssets = scene.Assets().Discover();
    if (!runtime.physicsLayersAsset.empty() &&
        !kb::scene::PhysicsBackend::LoadAndConfigureLayers(scene, runtime.physicsLayersAsset)) {
        err << "project physics layers could not be applied: " << runtime.physicsLayersAsset << '\n';
        return false;
    }

    if (!runtime.sceneReference.empty() && runtime.sceneReference.front() == '/') {
        const kb::assets::AssetMetadata* metadata =
            scene.Assets().Manager().Registry().FindByPath(runtime.sceneReference);
        if (metadata == nullptr) {
            err << "project scene asset was not found: " << runtime.sceneReference << '\n';
            return false;
        }
        loadedScenePath = metadata->physicalPath;
    } else {
        loadedScenePath = std::filesystem::path{ runtime.sceneReference };
        if (loadedScenePath.is_relative()) {
            loadedScenePath = runtime.projectRoot / loadedScenePath;
        }
    }
    if (!kb::scene::SceneDocumentService::LoadFileIntoScene(scene, loadedScenePath)) {
        err << "project scene could not be loaded: " << loadedScenePath.string() << '\n';
        return false;
    }

    kb::scene::SceneInputActivation::Apply(scene);
    if (runtime.inputEnabled && !runtime.inputMappingContext.empty()) {
        const kb::assets::AssetMetadata* input =
            scene.Assets().Manager().Registry().FindByPath(runtime.inputMappingContext);
        if (input == nullptr || input->type != "InputMappingContext" ||
            !scene.Input().AddMappingContext(input->id.value, 0)) {
            err << "project input mapping could not be activated: "
                << runtime.inputMappingContext << '\n';
            return false;
        }
    }
    return true;
}

} // namespace kb::game
