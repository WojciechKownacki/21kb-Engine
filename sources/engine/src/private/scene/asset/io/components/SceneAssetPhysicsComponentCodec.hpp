#pragma once

#include "engine/scene/CharacterControllerComponent.hpp"
#include "engine/scene/ColliderComponent.hpp"
#include "engine/scene/RigidbodyComponent.hpp"
#include "scene/asset/io/SceneAssetBinaryIO.hpp"

#include <cstdint>
#include <vector>

namespace kb::scene {

class SceneAssetPhysicsComponentCodec final {
public:
    SceneAssetPhysicsComponentCodec() = delete;

    [[nodiscard]] static bool ReadRigidbody(SceneAssetBinaryIO::ByteReader& input, RigidbodyComponent& output);
    static void WriteRigidbody(std::vector<std::uint8_t>& output, const RigidbodyComponent& rigidbody);

    [[nodiscard]] static bool ReadCollider(SceneAssetBinaryIO::ByteReader& input, ColliderComponent& output);
    static void WriteCollider(std::vector<std::uint8_t>& output, const ColliderComponent& collider);

    [[nodiscard]] static bool ReadCharacterController(SceneAssetBinaryIO::ByteReader& input, CharacterControllerComponent& output);
    static void WriteCharacterController(std::vector<std::uint8_t>& output, const CharacterControllerComponent& characterController);
};

} // namespace kb::scene
