#pragma once

#include <memory>
#include <string>

namespace kb::assets::bake {
struct BakeTargetProfile;
class RuntimeAssetPack;
}

namespace kb::render {

struct RuntimeAssetPackValidationResult final {
    std::string error;

    [[nodiscard]] bool Succeeded() const noexcept {
        return error.empty();
    }
};

// Performs the runtime-facing validation shared by the cooker publication gate and the
// standalone release validator. The caller owns mounting the immutable package and any
// game-module policy checks; this layer deliberately depends only on engine + renderer.
[[nodiscard]] RuntimeAssetPackValidationResult ValidateRuntimeAssetPack(
    const std::shared_ptr<kb::assets::bake::RuntimeAssetPack>& pack,
    const kb::assets::bake::BakeTargetProfile& expectedProfile);

} // namespace kb::render
