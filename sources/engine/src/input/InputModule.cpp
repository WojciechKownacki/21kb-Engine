#include "engine/input/InputModule.hpp"

#include "engine/assets/AssetId.hpp"
#include "engine/assets/AssetManager.hpp"
#include "engine/input/InputActionAsset.hpp"
#include "engine/input/InputMappingContextAsset.hpp"
#include "engine/input/InputPollingSystem.hpp"
#include "engine/input/InputSubsystem.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneAssets.hpp"
#include "engine/scene/SceneRuntime.hpp"

#include <cstdint>
#include <memory>

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
    // Resolve action / mapping-context asset references straight from this scene's
    // asset manager, then register the polling system (Input phase). Mirrors the
    // wiring previously performed in Scene's constructor.
    InputSubsystem& input = scene.Input();
    kb::assets::AssetManager& assetManager = scene.Assets().Manager();
    input.SetResolvers(
        [&assetManager](std::uint64_t id) {
            return assetManager.Load<InputActionAsset>(kb::assets::AssetId{ id }).Shared();
        },
        [&assetManager](std::uint64_t id) {
            return assetManager.Load<InputMappingContextAsset>(kb::assets::AssetId{ id }).Shared();
        });
    scene.Runtime().AddSceneSystem(std::make_unique<InputPollingSystem>(input));
}

} // namespace kb::input
