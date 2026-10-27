#pragma once

#include "engine/modules/EngineModuleMetadata.hpp"
#include "engine/modules/IEngineModule.hpp"
#include "engine/scene/SceneSystemHandle.hpp"
#include <unordered_map>

namespace kb::world2d {

class Map2DRuntime;

class World2DModule final : public kb::modules::IEngineModule {
public:
    [[nodiscard]] kb::modules::EngineModuleMetadata Metadata() const override;
    void OnLoad(kb::modules::EngineModuleContext& context) override;
    void OnUnload() override;
    void OnSceneAttach(kb::scene::Scene& scene) override;
    void OnSceneDetach(kb::scene::Scene& scene) override;
    [[nodiscard]] Map2DRuntime* RuntimeFor(kb::scene::Scene& scene) const noexcept;
private:
    struct Attached { kb::scene::SceneSystemHandle handle; Map2DRuntime* runtime = nullptr; kb::scene::SceneSystemHandle spriteHandle; kb::scene::SceneSystemHandle physicsHandle; };
    std::unordered_map<kb::scene::Scene*, Attached> scenes_;
};

} // namespace kb::world2d
