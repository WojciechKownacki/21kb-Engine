#include "GameProjectRuntime.hpp"
#include "PackagedRuntimeModuleContract.hpp"

#include "engine/assets/AssetManager.hpp"
#include "engine/assets/AssetMetadata.hpp"
#include "engine/assets/AssetRegistry.hpp"
#include "engine/assets/bake/RuntimeAssetPack.hpp"
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
#include "kb/render/resources/RenderMaterialFunctionAssetLoader.hpp"
#include "kb/render/resources/RenderMaterialGraphAssetLoader.hpp"
#include "kb/render/resources/RenderMaterialInstanceAssetLoader.hpp"
#include "kb/render/resources/RenderMaterialParameterCollection.hpp"
#include "kb/render/resources/RenderMaterialTypeAssetLoader.hpp"
#include "kb/render/resources/RenderMeshAssetLoader.hpp"
#include "kb/render/resources/RenderTextureAssetLoader.hpp"
#include "kb/render/runtime/RuntimeMaterialParameterValidation.hpp"

#if defined(_WIN32)
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #ifndef NOMINMAX
        #define NOMINMAX
    #endif
    #include <Windows.h>
#endif

#include <algorithm>
#include <cctype>
#include <limits>
#include <memory>
#include <ostream>
#include <system_error>
#include <utility>

namespace kb::game {

namespace {

#ifndef KB_GAME_TARGET_PROFILE_ID
#define KB_GAME_TARGET_PROFILE_ID ""
#endif

[[nodiscard]] std::string LowerExtension(const std::filesystem::path& path) {
    std::string extension = path.extension().string();
    std::ranges::transform(extension, extension.begin(), [](unsigned char value) {
        return static_cast<char>(std::tolower(value));
    });
    return extension;
}

#if defined(_WIN32)
[[nodiscard]] bool ResolvePackagedWindowsRuntimeModules(
    kb::project::ProjectDescriptor& descriptor,
    const std::filesystem::path& projectRoot,
    std::ostream& err) {
    std::error_code rootError;
    const std::filesystem::path absoluteRoot =
        std::filesystem::absolute(projectRoot, rootError).lexically_normal();
    const std::filesystem::path canonicalRoot =
        std::filesystem::weakly_canonical(absoluteRoot, rootError);
    if (rootError || !std::filesystem::is_directory(canonicalRoot, rootError) || rootError) {
        err << "packaged runtime root is unavailable\n";
        return false;
    }
    for (kb::project::ProjectPluginReference& plugin : descriptor.plugins) {
        if (!plugin.enabled) {
            continue;
        }
        const std::filesystem::path relativePath{ plugin.binaryPath };
        if (!IsSafeWindowsRuntimeModuleRelativePath(relativePath)) {
            err << "packaged Windows runtime module path is not a safe relative DLL: "
                << plugin.name << '\n';
            return false;
        }
        const std::filesystem::path unresolvedPath = canonicalRoot / relativePath;
        std::error_code moduleError;
        const std::filesystem::file_status unresolvedStatus =
            std::filesystem::symlink_status(unresolvedPath, moduleError);
        if (moduleError || unresolvedStatus.type() != std::filesystem::file_type::regular) {
            err << "packaged Windows runtime module DLL is missing: " << plugin.name << '\n';
            return false;
        }
        const std::filesystem::path resolvedPath =
            std::filesystem::weakly_canonical(unresolvedPath, moduleError);
        const std::filesystem::path relativeToRoot =
            resolvedPath.lexically_relative(canonicalRoot);
        if (moduleError || relativeToRoot.empty() || relativeToRoot.is_absolute() ||
            *relativeToRoot.begin() == "..") {
            err << "packaged Windows runtime module DLL escapes the package root: "
                << plugin.name << '\n';
            return false;
        }
        std::optional<std::string> exact = TryNarrow(resolvedPath.native());
        if (!exact.has_value()) {
            err << "packaged Windows runtime module path cannot be represented exactly: "
                << plugin.name << '\n';
            return false;
        }
        plugin.binaryPath = *std::move(exact);
    }
    return true;
}
#endif

[[nodiscard]] bool ReadPackagedGameProjectRuntime(
    const std::filesystem::path& packPath,
    std::string_view sceneOverride,
    GameProjectRuntime& runtime,
    std::ostream& err) {
    kb::assets::bake::BakeTargetProfile targetProfile{};
    if (!kb::assets::bake::TryFindBakeTargetProfile(
            KB_GAME_TARGET_PROFILE_ID, targetProfile)) {
        err << "runtime host has no valid package target identity\n";
        return false;
    }
    auto pack = std::make_shared<kb::assets::bake::RuntimeAssetPack>();
    const kb::assets::bake::RuntimeAssetPackStatus status =
        pack->Mount(packPath, targetProfile);
    if (status != kb::assets::bake::RuntimeAssetPackStatus::Success) {
        err << "runtime package could not be mounted: "
            << kb::assets::bake::ToString(status) << '\n';
        return false;
    }
    return ReadMountedGameProjectRuntime(
        std::move(pack), packPath.parent_path(), sceneOverride, runtime, err);
}

} // namespace

bool ReadMountedGameProjectRuntime(
    std::shared_ptr<kb::assets::bake::RuntimeAssetPack> pack,
    std::filesystem::path projectRoot,
    std::string_view sceneOverride,
    GameProjectRuntime& runtime,
    std::ostream& err) {
    if (pack == nullptr || !pack->IsMounted()) {
        err << "runtime package is not mounted\n";
        return false;
    }
    const kb::assets::bake::RuntimeAssetManifest& manifest = pack->Manifest();
    const std::string sceneReference = sceneOverride.empty()
        ? manifest.settings.defaultMap
        : std::string{ sceneOverride };
    const kb::assets::bake::RuntimeAssetManifestEntry* sceneAsset =
        pack->FindAsset(sceneReference);
    if (sceneReference.empty() || sceneReference.front() != '/' ||
        sceneAsset == nullptr || sceneAsset->type != "Scene" || !sceneAsset->runtimeLoadable) {
        err << "runtime package scene is missing or is not a loadable Scene: "
            << sceneReference << '\n';
        return false;
    }

    GameProjectRuntime packaged{};
    packaged.descriptor = manifest.descriptor;
    packaged.projectRoot = std::move(projectRoot);
#if defined(_WIN32)
    if (std::string_view{ KB_GAME_TARGET_PROFILE_ID } == "Windows.x64" &&
        !ResolvePackagedWindowsRuntimeModules(
            packaged.descriptor, packaged.projectRoot, err)) {
        return false;
    }
#endif
    packaged.gameName = manifest.settings.gameName.empty()
        ? manifest.settings.name
        : manifest.settings.gameName;
    packaged.sceneReference = sceneReference;
    packaged.physicsLayersAsset = manifest.settings.physicsLayersAsset;
    packaged.inputMappingContext = manifest.settings.inputMappingContext;
    packaged.inputEnabled = manifest.settings.inputEnabled;
    for (const kb::project::ProjectPluginReference& plugin : packaged.descriptor.plugins) {
        if (plugin.enabled && !plugin.name.empty()) {
            packaged.requiredModules.push_back(plugin.name);
        }
    }
    packaged.assetPack = std::move(pack);
    runtime = std::move(packaged);
    return true;
}

float RuntimeDeltaSeconds(
    std::chrono::steady_clock::time_point previous,
    std::chrono::steady_clock::time_point current) noexcept {
    const std::chrono::duration<float> delta = current - previous;
    return std::clamp(delta.count(), 0.0F, kMaximumRuntimeDeltaSeconds);
}

#if defined(_WIN32)
std::optional<std::string> TryNarrow(std::wstring_view text) {
    if (text.empty()) {
        return std::string{};
    }
    if (text.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
        return std::nullopt;
    }
    const UINT codePage = GetACP();
    // WC_NO_BEST_FIT_CHARS and a replacement character are rejected outright for
    // UTF-8, which is also the one code page that can spell everything.
    const bool utf8 = codePage == CP_UTF8;
    const DWORD flags = utf8 ? 0UL : static_cast<DWORD>(WC_NO_BEST_FIT_CHARS);
    const char* const replacement = utf8 ? nullptr : "?";
    const auto length = static_cast<int>(text.size());
    const int required =
        WideCharToMultiByte(codePage, flags, text.data(), length, nullptr, 0, replacement, nullptr);
    if (required <= 0) {
        return std::nullopt;
    }
    std::string narrow(static_cast<std::size_t>(required), '\0');
    BOOL usedReplacement = FALSE;
    const int written = WideCharToMultiByte(
        codePage,
        flags,
        text.data(),
        length,
        narrow.data(),
        required,
        replacement,
        utf8 ? nullptr : &usedReplacement);
    if (written <= 0 || usedReplacement != FALSE) {
        return std::nullopt;
    }
    narrow.resize(static_cast<std::size_t>(written));
    return narrow;
}

std::string NarrowForDiagnostics(std::wstring_view text) {
    if (std::optional<std::string> exact = TryNarrow(text); exact.has_value()) {
        return *std::move(exact);
    }
    if (text.empty() || text.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
        return std::string{ "<unprintable>" };
    }
    const auto length = static_cast<int>(text.size());
    const int required =
        WideCharToMultiByte(CP_ACP, 0UL, text.data(), length, nullptr, 0, "?", nullptr);
    if (required <= 0) {
        return std::string{ "<unprintable>" };
    }
    std::string narrow(static_cast<std::size_t>(required), '\0');
    const int written = WideCharToMultiByte(
        CP_ACP, 0UL, text.data(), length, narrow.data(), required, "?", nullptr);
    if (written <= 0) {
        return std::string{ "<unprintable>" };
    }
    narrow.resize(static_cast<std::size_t>(written));
    return narrow;
}

#endif

std::string NarrowForDiagnostics(const std::filesystem::path& path) {
#if defined(_WIN32)
    return NarrowForDiagnostics(std::wstring_view{ path.native() });
#else
    return path.native();
#endif
}

#if defined(_WIN32)
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
#endif

bool ReadGameProjectRuntime(
    const std::filesystem::path& projectPath,
    std::string_view sceneOverride,
    GameProjectRuntime& runtime,
    std::ostream& err) {
    std::error_code pathError;
    const std::filesystem::path absoluteInput =
        std::filesystem::absolute(projectPath, pathError).lexically_normal();
    if (pathError) {
        err << "project path could not be resolved: " << NarrowForDiagnostics(projectPath) << '\n';
        return false;
    }

    std::filesystem::path packageCandidate = absoluteInput;
    if (std::filesystem::is_directory(absoluteInput, pathError) && !pathError) {
        packageCandidate /= "Game.kbpack";
    }
    if (!pathError && std::filesystem::is_regular_file(packageCandidate, pathError) && !pathError &&
        LowerExtension(packageCandidate) == kb::assets::bake::kAssetPackFileExtension) {
        return ReadPackagedGameProjectRuntime(packageCandidate, sceneOverride, runtime, err);
    }
    pathError.clear();

    std::filesystem::path projectFile = absoluteInput;
    if (std::filesystem::is_directory(absoluteInput, pathError) && !pathError) {
        projectFile /= "Project.21kbproject";
    }
    if (pathError || !std::filesystem::is_regular_file(projectFile, pathError) || pathError) {
        err << "project descriptor was not found: " << NarrowForDiagnostics(projectFile) << '\n';
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
#if defined(_WIN32)
            // A project-local binary is only worth naming when the name survives
            // the trip through the process code page; a path that cannot be
            // spelled exactly keeps the portable filename the descriptor stored.
            if (std::optional<std::string> exact = TryNarrow(projectLocalPath.native());
                exact.has_value()) {
                plugin.binaryPath = *std::move(exact);
            }
#else
            // POSIX paths are byte strings. Preserve the exact spelling that was
            // successfully used for the filesystem probe above.
            plugin.binaryPath = projectLocalPath.native();
#endif
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
    if (!manager.RegisterLoader(std::make_unique<kb::render::RenderMaterialFunctionAssetLoader>())) {
        err << "material function loader registration failed\n";
        return false;
    }
    if (!manager.RegisterLoader(std::make_unique<kb::render::RenderMaterialGraphAssetLoader>())) {
        err << "material graph loader registration failed\n";
        return false;
    }
    if (!manager.RegisterLoader(std::make_unique<kb::render::RenderMaterialInstanceAssetLoader>())) {
        err << "material instance loader registration failed\n";
        return false;
    }
    if (!manager.RegisterLoader(std::make_unique<kb::render::RenderMaterialParameterCollectionAssetLoader>())) {
        err << "material parameter collection loader registration failed\n";
        return false;
    }
    if (!manager.RegisterLoader(std::make_unique<kb::render::RenderMaterialTypeAssetLoader>())) {
        err << "material type loader registration failed\n";
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
    if (runtime.IsPackaged()) {
        if (!scene.Assets().Manager().MountRuntimePack(runtime.assetPack)) {
            err << "runtime package registry could not be mounted: "
                << scene.Assets().Manager().LastError() << '\n';
            return false;
        }
        discoveredAssets = runtime.assetPack->Manifest().assets.size();
    } else {
        if (!scene.Assets().MountProject(runtime.projectRoot)) {
            err << "project assets could not be mounted: " << NarrowForDiagnostics(runtime.projectRoot) << '\n';
            return false;
        }
        discoveredAssets = scene.Assets().Discover();
    }
    if (!runtime.physicsLayersAsset.empty() &&
        !kb::scene::PhysicsBackend::LoadAndConfigureLayers(scene, runtime.physicsLayersAsset)) {
        err << "project physics layers could not be applied: " << runtime.physicsLayersAsset << '\n';
        return false;
    }

    if (!runtime.sceneReference.empty() && runtime.sceneReference.front() == '/') {
        const kb::assets::AssetMetadata* metadata =
            scene.Assets().Manager().Registry().FindByPath(runtime.sceneReference);
        if (metadata == nullptr || metadata->type != "Scene") {
            err << "project scene asset was not found: " << runtime.sceneReference << '\n';
            return false;
        }
        const kb::assets::AssetHandle<kb::scene::SceneDocument> document =
            scene.Assets().Manager().Load<kb::scene::SceneDocument>(metadata->id);
        if (!document.IsLoaded() ||
            !kb::scene::SceneDocumentService::LoadIntoScene(scene, *document)) {
            err << "project scene could not be loaded: " << runtime.sceneReference;
            const std::string loadError = scene.Assets().Manager().LastError();
            if (!loadError.empty()) {
                err << " (" << loadError << ")";
            }
            err << '\n';
            return false;
        }
        loadedScenePath = runtime.IsPackaged()
            ? std::filesystem::path{ runtime.sceneReference }
            : metadata->physicalPath;
    } else {
        if (runtime.IsPackaged()) {
            err << "packaged project scene must use a /Game virtual path\n";
            return false;
        }
        loadedScenePath = std::filesystem::path{ runtime.sceneReference };
        if (loadedScenePath.is_relative()) {
            loadedScenePath = runtime.projectRoot / loadedScenePath;
        }
        if (!kb::scene::SceneDocumentService::LoadFileIntoScene(scene, loadedScenePath)) {
            err << "project scene could not be loaded: " << NarrowForDiagnostics(loadedScenePath) << '\n';
            return false;
        }
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
