#include "PackagedGameRuntime.hpp"

#include "PackagedRuntimeModules.hpp"
#include "GameRuntimeDiagnostics.hpp"

#include "engine/assets/bake/BakeTargetProfile.hpp"
#include "engine/assets/bake/RuntimeAssetPack.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneEntities.hpp"
#include "engine/scene/SceneRuntime.hpp"
#include "engine/scene/SceneUI.hpp"
#include "engine/script/ScriptModule.hpp"
#include "engine/script/ScriptRuntimeHost.hpp"
#include "kb/render/Renderer.hpp"
#include "kb/render/RuntimeAssetShaderProvider.hpp"
#include "GameProjectLighting.hpp"

#include <filesystem>
#include <chrono>
#include <memory>
#include <ostream>
#include <string>
#include <utility>

#ifndef KB_GAME_TARGET_PROFILE_ID
#define KB_GAME_TARGET_PROFILE_ID ""
#endif

namespace kb::game {

bool RuntimeHostBakeTargetProfile(kb::assets::bake::BakeTargetProfile& profile) noexcept {
    return kb::assets::bake::TryFindBakeTargetProfile(KB_GAME_TARGET_PROFILE_ID, profile);
}

PackagedGameRuntime::~PackagedGameRuntime() = default;

bool PackagedGameRuntime::ConfigureNetwork(kb::net::SceneClientOptions options,
    kb::net::SceneClient::TransportFactory factory) {
    if (scene_ == nullptr || ticked_ || network_) return false;
    network_ = std::make_unique<kb::net::SceneClient>(*scene_, std::move(options), std::move(factory));
    return true;
}

bool PackagedGameRuntime::Initialize(
    std::shared_ptr<kb::assets::bake::RuntimeAssetPack> pack,
    std::filesystem::path storageRoot,
    kb::render::Renderer& renderer,
    std::ostream& diagnostics) {
    if (pack == nullptr || !pack->IsMounted() || scene_ != nullptr) {
        diagnostics << "packaged runtime initialization received invalid state\n";
        return false;
    }

    std::string providerError;
    const std::shared_ptr<kb::render::RuntimeAssetShaderProvider> provider =
        kb::render::RuntimeAssetShaderProvider::Create(pack, providerError);
    if (provider == nullptr || !renderer.SetShaderBinaryProvider(provider)) {
        diagnostics << "packaged shader provider could not be configured";
        if (!providerError.empty()) {
            diagnostics << ": " << providerError;
        }
        diagnostics << '\n';
        return false;
    }

    // The platform's writable per-game storage also holds script save slots.
    std::filesystem::path scriptUserStorageRoot =
        storageRoot.empty() ? std::filesystem::path{} : storageRoot / "Saves";
    GameProjectRuntime project{};
    if (!ReadMountedGameProjectRuntime(
            std::move(pack), std::move(storageRoot), {}, project, diagnostics)) {
        return false;
    }

    PackagedRuntimeModules staticModules{};
    std::string moduleError;
    if (!CreatePackagedRuntimeModules(
            project.descriptor, staticModules, moduleError, std::move(scriptUserStorageRoot))) {
        diagnostics << moduleError << '\n';
        return false;
    }

    script_ = staticModules.script;
    auto scene = std::make_unique<kb::scene::Scene>(
        std::move(project.descriptor),
        std::move(staticModules.modules),
        kb::ecs::WorldConfig{},
        kb::scene::SceneMode::Runtime);
    scriptActive_ = scene->IsModuleActive("Script");
    if (scriptActive_ &&
        (script_ == nullptr || !script_->Succeeded() || script_->Host() == nullptr)) {
        diagnostics << "script module initialization failed\n";
        if (script_ != nullptr) {
            for (const std::string& diagnostic : script_->Diagnostics()) {
                diagnostics << "script module diagnostic: " << diagnostic << '\n';
            }
        }
        script_ = nullptr;
        scriptActive_ = false;
        return false;
    }

    std::filesystem::path scenePath;
    std::size_t discoveredAssets = 0U;
    if (scriptActive_) {
        script_->Host()->AssetPreparer().SetNativeSettings({
            .buildPlugins = false,
            .runtimeModuleRoot = project.projectRoot,
        });
    }
    if (!LoadGameProjectScene(project, *scene, scenePath, discoveredAssets, diagnostics)) {
        script_ = nullptr;
        scriptActive_ = false;
        return false;
    }

    diagnostics << "project loaded: scene=" << scenePath.generic_string()
                << " entities=" << scene->Entities().Count()
                << " assets=" << discoveredAssets
                << " modules=" << scene->ActiveModuleCount() << '\n';
    project_ = std::move(project);
    scene_ = std::move(scene);
    runtimeClean_ = true;
    lightingApplied_ = false;
    reportedMaterialShadingFallback_ = false;
    reportedGraphProgramFallback_ = false;
    renderSceneSync_.Reset();
    return true;
}

bool PackagedGameRuntime::Tick(
    kb::render::Renderer& renderer,
    float deltaSeconds,
    std::ostream& diagnostics,
    bool* frameSubmitted) {
    if (frameSubmitted != nullptr) {
        *frameSubmitted = false;
    }
    if (!runtimeClean_ || scene_ == nullptr || scene_->Runtime().ShouldQuit()) {
        return false;
    }
    static_cast<void>(scene_->UI().SetViewport(
        static_cast<float>(renderer.BackbufferWidth()),
        static_cast<float>(renderer.BackbufferHeight())));
    renderSceneSync_.BeforeUpdate(*scene_);
    static_cast<void>(scene_->Runtime().Update(deltaSeconds));
    ticked_ = true;
    for (const std::string& error : DrainGameRuntimeErrors(*scene_, scriptActive_ ? script_ : nullptr)) {
        diagnostics << error << '\n';
        runtimeClean_ = false;
    }
    if (!runtimeClean_) {
        return false;
    }
    renderer.SetFrameDeltaSeconds(deltaSeconds);
    // The platform initializes (or recreates) its renderer after loading the pack.
    // Apply the project's path on its first frame instead of silently using the host default.
    if (!lightingApplied_) {
        ApplyGameProjectLighting(renderer, project_, diagnostics);
        lightingApplied_=true;
    }
    if (!renderer.BeginFrame()) {
        diagnostics << "renderer could not begin a frame\n";
        runtimeClean_ = false;
        return false;
    }
    const bool submitted = renderSceneSync_.Submit(*scene_, renderer);
    // A capability reduction or absent graph program must be visible in shipped
    // host logs. Report each category once per runtime, without per-frame log growth.
    ReportGameMaterialFallbacks(renderer, reportedMaterialShadingFallback_, reportedGraphProgramFallback_, diagnostics);
    renderer.EndFrame();
    if (!submitted) {
        diagnostics << "renderer could not submit the scene\n";
        runtimeClean_ = false;
        return false;
    }
    // Finish initial shader preparation before starting the connection deadline.
    if (network_) {
        const auto now = static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count());
        if (networkStarted_ == 0U) networkStarted_ = now;
        if (!network_->Update(now - networkStarted_, diagnostics)) runtimeClean_ = false;
    }
    if (!runtimeClean_) return false;
    if (frameSubmitted != nullptr) {
        *frameSubmitted = true;
    }
    return !scene_->Runtime().ShouldQuit();
}

void PackagedGameRuntime::ReleaseRenderResources(kb::render::Renderer& renderer) {
    if (scene_ != nullptr && renderer.IsInitialized()) {
        renderer.ReleaseScene(*scene_);
    }
    renderSceneSync_.Reset();
    lightingApplied_ = false;
}

bool PackagedGameRuntime::Shutdown(
    kb::render::Renderer& renderer,
    std::ostream& diagnostics) {
    bool clean = runtimeClean_;
    if (scriptActive_ && script_ != nullptr && script_->Host() != nullptr &&
        !script_->Host()->DispatchShutdownLifecycle(0.0F)) {
        diagnostics << "script shutdown lifecycle could not be dispatched\n";
        clean = false;
    }
    if (scene_ != nullptr) {
        for (const std::string& error : DrainGameRuntimeErrors(*scene_, scriptActive_ ? script_ : nullptr)) {
            diagnostics << error << '\n';
            clean = false;
        }
    }
    ReleaseRenderResources(renderer);
    network_.reset();
    scene_.reset();
    networkStarted_ = 0U;
    ticked_ = false;
    script_ = nullptr;
    scriptActive_ = false;
    project_ = {};
    runtimeClean_ = clean;
    return clean;
}

kb::scene::Scene* PackagedGameRuntime::Scene() noexcept {
    return scene_.get();
}

const GameProjectRuntime& PackagedGameRuntime::Project() const noexcept {
    return project_;
}

} // namespace kb::game
