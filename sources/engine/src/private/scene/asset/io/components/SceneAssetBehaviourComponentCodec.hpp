#pragma once

#include "engine/scene/BehaviourComponent.hpp"
#include "engine/scene/BehaviourVariableOverride.hpp"
#include "scene/asset/io/SceneAssetBinaryIO.hpp"

#include <cstdint>
#include <span>
#include <vector>

namespace kb::scene {

class SceneAssetBehaviourComponentCodec final {
public:
    SceneAssetBehaviourComponentCodec() = delete;

    // Files older than SceneDocument::BehaviourVariableOverridesFileVersion carry no variable overrides.
    [[nodiscard]] static bool Read(SceneAssetBinaryIO::ByteReader& input, std::uint32_t fileVersion, BehaviourComponent& output,
        std::vector<BehaviourVariableOverride>& variableOverrides);
    static void Write(std::vector<std::uint8_t>& output, const BehaviourComponent& behaviour,
        std::span<const BehaviourVariableOverride> variableOverrides);
};

} // namespace kb::scene
