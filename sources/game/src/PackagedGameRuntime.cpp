#include "PackagedGameRuntime.hpp"

#include "PackagedRuntimeModules.hpp"

#include "engine/assets/bake/BakeTargetProfile.hpp"
#include "engine/assets/bake/RuntimeAssetPack.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneEntities.hpp"
#include "engine/scene/SceneRuntime.hpp"
#include "engine/script/ScriptModule.hpp"
#include "engine/script/ScriptRuntimeHost.hpp"
#include "kb/render/Renderer.hpp"
#include "kb/render/RuntimeAssetShaderProvider.hpp"

#include <filesystem>
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

    GameProjectRuntime project{};
    if (!ReadMountedGameProjectRuntime(
            std::move(pack), std::move(storageRoot), {}, project, diagnostics)) {
        return false;
    }

    PackagedRuntimeModules staticModules{};
    std::string moduleError;
    if (!CreatePackagedRuntimeModules(project.descriptor, staticModules, moduleError)) {
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
    return true;
}

bool PackagedGameRuntime::Tick(
    kb::render::Renderer& renderer,
    float deltaSeconds,
    bool* frameSubmitted) {
    if (frameSubmitted != nullptr) {
        *frameSubmitted = false;
    }
    if (scene_ == nullptr || scene_->Runtime().ShouldQuit()) {
        return false;
    }
    static_cast<void>(scene_->Runtime().Update(deltaSeconds));
    if (renderer.BeginFrame()) {
        renderer.SubmitScene(*scene_);
        renderer.EndFrame();
        if (frameSubmitted != nullptr) {
            *frameSubmitted = true;
        }
    }
    return !scene_->Runtime().ShouldQuit();
}

bool PackagedGameRuntime::Shutdown(
    kb::render::Renderer& renderer,
    std::ostream& diagnostics) {
    bool clean = true;
    if (scriptActive_ && script_ != nullptr && script_->Host() != nullptr &&
        !script_->Host()->DispatchShutdownLifecycle(0.0F)) {
        diagnostics << "script shutdown lifecycle could not be dispatched\n";
        clean = false;
    }
    if (scene_ != nullptr && renderer.IsInitialized()) {
        renderer.ReleaseScene(*scene_);
    }
    scene_.reset();
    script_ = nullptr;
    scriptActive_ = false;
    project_ = {};
    return clean;
}

kb::scene::Scene* PackagedGameRuntime::Scene() noexcept {
    return scene_.get();
}

const GameProjectRuntime& PackagedGameRuntime::Project() const noexcept {
    return project_;
}

} // namespace kb::game
