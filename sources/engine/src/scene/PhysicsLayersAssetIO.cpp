#include "engine/scene/PhysicsLayersAssetIO.hpp"

#include "scene/asset/io/SceneAssetBinaryIO.hpp"

#include <array>

namespace kb::scene {
namespace {

// Reuse the engine's byte-level (de)serialization helpers; the same ones
// kb::input::InputAssetIO uses.
namespace io = kb::scene::SceneAssetBinaryIO;

[[nodiscard]] bool ReadMagic(io::ByteReader& reader) {
    std::array<std::uint8_t, 8U> magic{};
    if (!reader.ReadRaw(magic.data(), magic.size())) {
        return false;
    }
    return magic == PhysicsLayersAssetFormat::Magic;
}

[[nodiscard]] PhysicsLayersAssetLoadResult Fail(std::string error) {
    return PhysicsLayersAssetLoadResult{ .succeeded = false, .asset = {}, .error = std::move(error) };
}

} // namespace

std::vector<std::uint8_t> EncodePhysicsLayersAsset(const PhysicsLayersAsset& asset) {
    std::vector<std::uint8_t> output;
    io::WriteRaw(output, PhysicsLayersAssetFormat::Magic.data(), PhysicsLayersAssetFormat::Magic.size());
    io::WriteUInt32(output, PhysicsLayersAssetFormat::BinaryVersion);
    for (const std::string& name : asset.layerNames) {
        io::WriteString(output, name);
    }
    for (const std::array<bool, kPhysicsLayerCount>& row : asset.interactionMatrix) {
        for (const bool interacts : row) {
            io::WriteBool(output, interacts);
        }
    }
    return output;
}

PhysicsLayersAssetLoadResult DecodePhysicsLayersAsset(std::span<const std::uint8_t> bytes) {
    io::ByteReader reader(std::vector<std::uint8_t>(bytes.begin(), bytes.end()));
    if (!ReadMagic(reader)) {
        return Fail("Invalid physics layers asset magic");
    }
    std::uint32_t version = 0U;
    if (!reader.ReadUInt32(version) || version != PhysicsLayersAssetFormat::BinaryVersion) {
        return Fail("Unsupported physics layers asset version");
    }

    PhysicsLayersAsset asset;
    for (std::string& name : asset.layerNames) {
        if (!reader.ReadString(name, PhysicsLayersAssetFormat::MaxNameBytes)) {
            return Fail("Corrupt physics layers asset payload");
        }
    }
    for (std::array<bool, kPhysicsLayerCount>& row : asset.interactionMatrix) {
        for (bool& interacts : row) {
            if (!reader.ReadBool(interacts)) {
                return Fail("Corrupt physics layers asset interaction matrix");
            }
        }
    }
    return PhysicsLayersAssetLoadResult{ .succeeded = true, .asset = std::move(asset), .error = {} };
}

PhysicsLayersAssetLoadResult ReadPhysicsLayersAsset(const std::filesystem::path& path) {
    return DecodePhysicsLayersAsset(io::ReadAllBytes(path));
}

bool WritePhysicsLayersAsset(const std::filesystem::path& path, const PhysicsLayersAsset& asset) {
    return io::WriteBytesAtomically(path, EncodePhysicsLayersAsset(asset));
}

} // namespace kb::scene
