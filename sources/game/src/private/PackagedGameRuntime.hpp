#pragma once

#include "GameProjectRuntime.hpp"

#include <filesystem>
#include <iosfwd>
#include <memory>

namespace kb::assets::bake {
struct BakeTargetProfile;
class RuntimeAssetPack;
}

namespace kb::render {
class Renderer;
}

namespace kb::scene {
class Scene;
}

namespace kb::script {
class ScriptModule;
}

namespace kb::game {

// Returns the immutable cook identity compiled into this host configuration.
// CMake defines it per Android texture mode and per browser renderer.
[[nodiscard]] bool RuntimeHostBakeTargetProfile(
    kb::assets::bake::BakeTargetProfile& profile) noexcept;

class PackagedGameRuntime {
public:
    PackagedGameRuntime() = default;
    PackagedGameRuntime(const PackagedGameRuntime&) = delete;
    PackagedGameRuntime& operator=(const PackagedGameRuntime&) = delete;
    ~PackagedGameRuntime();

    [[nodiscard]] bool Initialize(
        std::shared_ptr<kb::assets::bake::RuntimeAssetPack> pack,
        std::filesystem::path storageRoot,
        kb::render::Renderer& renderer,
        std::ostream& diagnostics);
    [[nodiscard]] bool Tick(
        kb::render::Renderer& renderer,
        float deltaSeconds,
        bool* frameSubmitted = nullptr);
    [[nodiscard]] bool Shutdown(kb::render::Renderer& renderer, std::ostream& diagnostics);

    [[nodiscard]] kb::scene::Scene* Scene() noexcept;
    [[nodiscard]] const GameProjectRuntime& Project() const noexcept;

private:
    GameProjectRuntime project_{};
    std::unique_ptr<kb::scene::Scene> scene_;
    kb::script::ScriptModule* script_ = nullptr;
    bool scriptActive_ = false;
};

} // namespace kb::game
