#include "engine/library/EngineLibraryTaskFactories.hpp"

#include "engine/assets/AssetManager.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneLoadedContent.hpp"
#include "engine/script/ScriptEventBus.hpp"

#include <utility>

namespace kb::library {

std::function<kb::scene::TaskPollResult(float)> MakeWaitEventTask(
    std::shared_ptr<const kb::script::ScriptEventObservation> observation) {
    const std::uint64_t initialSequence = observation ? observation->Sequence() : 0U;
    return [observation = std::move(observation), initialSequence](float) -> kb::scene::TaskPollResult {
        if (!observation) {
            return kb::scene::TaskPollResult::Failed;
        }
        return observation->Sequence() != initialSequence
            ? kb::scene::TaskPollResult::Completed
            : kb::scene::TaskPollResult::Running;
    };
}

std::function<kb::scene::TaskPollResult(float)> MakeWaitAssetLoadTask(
    const kb::assets::AssetManager& assets,
    kb::assets::AssetId assetId) {
    return [&assets, assetId](float) -> kb::scene::TaskPollResult {
        if (!assetId.IsValid() || assets.Registry().Find(assetId) == nullptr) {
            return kb::scene::TaskPollResult::Failed;
        }
        return assets.IsLoaded(assetId)
            ? kb::scene::TaskPollResult::Completed
            : kb::scene::TaskPollResult::Running;
    };
}

std::function<kb::scene::TaskPollResult(float)> MakeWaitSceneLoadTask(
    const kb::scene::Scene& scene,
    std::string sceneName) {
    return [&scene, sceneName = std::move(sceneName)](float) -> kb::scene::TaskPollResult {
        if (sceneName.empty()) {
            return kb::scene::TaskPollResult::Failed;
        }
        return scene.LoadedContent().Find(sceneName) != 0U
            ? kb::scene::TaskPollResult::Completed
            : kb::scene::TaskPollResult::Running;
    };
}

} // namespace kb::library
