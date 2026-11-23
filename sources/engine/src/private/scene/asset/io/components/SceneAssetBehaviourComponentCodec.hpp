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

    // The behaviours of a node after its first one: a count (1 to kMaxBehavioursPerEntity - 1) and the behaviours. A node
    // without any is written exactly as before they existed, so older files and files without them have the same bytes.
    [[nodiscard]] static bool ReadAdditional(SceneAssetBinaryIO::ByteReader& input, std::vector<BehaviourComponent>& additional);
    static void WriteAdditional(std::vector<std::uint8_t>& output, std::span<const BehaviourComponent> additional);
};

} // namespace kb::scene
