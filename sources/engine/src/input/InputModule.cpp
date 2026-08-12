#include "engine/input/InputModule.hpp"

#include "engine/assets/AssetId.hpp"
#include "engine/assets/AssetManager.hpp"
#include "engine/input/InputActionAsset.hpp"
#include "engine/input/InputMappingContextAsset.hpp"
#include "engine/input/InputPollingSystem.hpp"
#include "engine/input/InputSubsystem.hpp"
#include "engine/library/EngineLibraryAssetRef.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneAssets.hpp"
#include "engine/scene/SceneRuntime.hpp"

#include <cstdint>
#include <memory>
#include <stdexcept>

namespace kb::input {

kb::modules::EngineModuleMetadata InputModule::Metadata() const {
    return kb::modules::EngineModuleMetadata{
        "Input",
        1U,
        {},
        kb::modules::EngineModuleLoadingPhase::PreDefault,
    };
}

void InputModule::OnSceneAttach(kb::scene::Scene& scene) {
    if (sceneSystems_.contains(&scene)) {
        return;
    }
    // Resolve action / mapping-context asset references straight from this scene's
    // asset manager, then register the polling system (Input phase). Mirrors the
    // wiring previously performed in Scene's constructor.
    InputSubsystem& input = scene.Input();
    kb::assets::AssetManager& assetManager = scene.Assets().Manager();
    input.SetResolvers(
        [&assetManager](std::uint64_t id) {
            const kb::library::InputActionRef action = assetManager.Load<InputActionAsset>(kb::assets::AssetId{ id });
            return action.Shared();
        },
        [&assetManager](std::uint64_t id) {
            const kb::library::InputMapRef mapping = assetManager.Load<InputMappingContextAsset>(kb::assets::AssetId{ id });
            return mapping.Shared();
        });
    const kb::scene::SceneSystemHandle handle = scene.Runtime().AddSceneSystem(std::make_unique<InputPollingSystem>());
    if (handle.IsValid()) {
        sceneSystems_.emplace(&scene, handle);
    }
}

void InputModule::OnSceneDetach(kb::scene::Scene& scene) {
    const auto iterator = sceneSystems_.find(&scene);
    if (iterator == sceneSystems_.end()) {
        return;
    }
    if (!scene.Runtime().RemoveSceneSystem(iterator->second)
        && scene.Runtime().HasSceneSystem(iterator->second)) {
        throw std::logic_error("input scene system could not detach during active dispatch");
    }
    sceneSystems_.erase(iterator);
}

} // namespace kb::input
