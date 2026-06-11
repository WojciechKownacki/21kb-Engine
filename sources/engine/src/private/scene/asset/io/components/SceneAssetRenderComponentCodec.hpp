#pragma once

#include "engine/scene/LightComponent.hpp"
#include "engine/scene/MeshRendererComponent.hpp"
#include "scene/asset/io/SceneAssetBinaryIO.hpp"

#include <cstdint>
#include <vector>

namespace kb::scene {

class SceneAssetRenderComponentCodec final {
public:
    SceneAssetRenderComponentCodec() = delete;

    [[nodiscard]] static bool ReadMeshRenderer(SceneAssetBinaryIO::ByteReader& input, MeshRendererComponent& output);
    static void WriteMeshRenderer(std::vector<std::uint8_t>& output, const MeshRendererComponent& meshRenderer);

    [[nodiscard]] static bool ReadLight(SceneAssetBinaryIO::ByteReader& input, LightComponent& output);
    static void WriteLight(std::vector<std::uint8_t>& output, const LightComponent& light);
};

} // namespace kb::scene
