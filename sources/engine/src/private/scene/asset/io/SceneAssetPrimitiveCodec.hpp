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

    [[nodiscard]] static bool ReadDVec3(SceneAssetBinaryIO::ByteReader& input, kb::math::DVec3& output) {
        return input.ReadDouble(output.x) && input.ReadDouble(output.y) && input.ReadDouble(output.z);
    }

    static void WriteDVec3(std::vector<std::uint8_t>& output, const kb::math::DVec3& value) {
        SceneAssetBinaryIO::WriteDouble(output, value.x);
        SceneAssetBinaryIO::WriteDouble(output, value.y);
        SceneAssetBinaryIO::WriteDouble(output, value.z);
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
