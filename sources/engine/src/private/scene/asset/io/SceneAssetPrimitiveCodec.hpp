#pragma once

#include "engine/scene/TransformComponent.hpp"
#include "scene/asset/io/SceneAssetBinaryIO.hpp"

#include <cstdint>
#include <vector>

namespace kb::scene {

class SceneAssetPrimitiveCodec final {
public:
    SceneAssetPrimitiveCodec() = delete;

    [[nodiscard]] static bool ReadVec3(SceneAssetBinaryIO::ByteReader& input, Vec3& output) {
        return input.ReadFloat(output.x) && input.ReadFloat(output.y) && input.ReadFloat(output.z);
    }

    [[nodiscard]] static bool ReadQuat(SceneAssetBinaryIO::ByteReader& input, Quat& output) {
        return input.ReadFloat(output.x) && input.ReadFloat(output.y) && input.ReadFloat(output.z) && input.ReadFloat(output.w);
    }

    static void WriteVec3(std::vector<std::uint8_t>& output, Vec3 value) {
        SceneAssetBinaryIO::WriteFloat(output, value.x);
        SceneAssetBinaryIO::WriteFloat(output, value.y);
        SceneAssetBinaryIO::WriteFloat(output, value.z);
    }

    static void WriteQuat(std::vector<std::uint8_t>& output, Quat value) {
        SceneAssetBinaryIO::WriteFloat(output, value.x);
        SceneAssetBinaryIO::WriteFloat(output, value.y);
        SceneAssetBinaryIO::WriteFloat(output, value.z);
        SceneAssetBinaryIO::WriteFloat(output, value.w);
    }
};

} // namespace kb::scene
