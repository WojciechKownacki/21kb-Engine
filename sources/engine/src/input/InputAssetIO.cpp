#include "engine/input/InputAssetIO.hpp"

#include "scene/asset/io/SceneAssetBinaryIO.hpp"

#include <array>
#include <cstddef>

namespace kb::input {
namespace {

// Reuse the engine's byte-level (de)serialization helpers; they are generic and
// not scene-specific despite their namespace.
namespace io = kb::scene::SceneAssetBinaryIO;

[[nodiscard]] bool ReadMagic(io::ByteReader& reader, const std::array<std::uint8_t, 8U>& expected) {
    std::array<std::uint8_t, 8U> magic{};
    if (!reader.ReadRaw(magic.data(), magic.size())) {
        return false;
    }
    return magic == expected;
}

void WriteModifier(std::vector<std::uint8_t>& output, const InputModifierDesc& modifier) {
    io::WriteUInt8(output, static_cast<std::uint8_t>(modifier.type));
    for (const float value : modifier.params) {
        io::WriteFloat(output, value);
    }
}

[[nodiscard]] bool ReadModifier(io::ByteReader& reader, InputModifierDesc& modifier) {
    std::uint8_t type = 0U;
    if (!reader.ReadUInt8(type) || type > static_cast<std::uint8_t>(InputModifierType::FovScaling)) {
        return false;
    }
    modifier.type = static_cast<InputModifierType>(type);
    for (float& value : modifier.params) {
        if (!reader.ReadFloat(value)) {
            return false;
        }
    }
    return true;
}

void WriteTrigger(std::vector<std::uint8_t>& output, const InputTriggerDesc& trigger) {
    io::WriteUInt8(output, static_cast<std::uint8_t>(trigger.type));
    for (const float value : trigger.params) {
        io::WriteFloat(output, value);
    }
    io::WriteUInt64(output, trigger.chordActionId);
}

[[nodiscard]] bool ReadTrigger(io::ByteReader& reader, InputTriggerDesc& trigger) {
    std::uint8_t type = 0U;
    if (!reader.ReadUInt8(type) || type > static_cast<std::uint8_t>(InputTriggerType::Chorded)) {
        return false;
    }
    trigger.type = static_cast<InputTriggerType>(type);
    for (float& value : trigger.params) {
        if (!reader.ReadFloat(value)) {
            return false;
        }
    }
    return reader.ReadUInt64(trigger.chordActionId);
}

void WriteMapping(std::vector<std::uint8_t>& output, const InputKeyMapping& mapping) {
    io::WriteUInt64(output, mapping.actionId);
    io::WriteUInt32(output, static_cast<std::uint32_t>(mapping.key));
    io::WriteFloat(output, mapping.scale);
    io::WriteUInt32(output, static_cast<std::uint32_t>(mapping.modifiers.size()));
    for (const InputModifierDesc& modifier : mapping.modifiers) {
        WriteModifier(output, modifier);
    }
    io::WriteUInt32(output, static_cast<std::uint32_t>(mapping.triggers.size()));
    for (const InputTriggerDesc& trigger : mapping.triggers) {
        WriteTrigger(output, trigger);
    }
}

[[nodiscard]] bool ReadMapping(io::ByteReader& reader, InputKeyMapping& mapping) {
    std::uint32_t key = 0U;
    if (!reader.ReadUInt64(mapping.actionId) || !reader.ReadUInt32(key) || !reader.ReadFloat(mapping.scale)) {
        return false;
    }
    mapping.key = static_cast<InputKey>(static_cast<std::uint16_t>(key));

    std::uint32_t modifierCount = 0U;
    if (!reader.ReadUInt32(modifierCount) || modifierCount > InputAssetFormat::MaxStackCount) {
        return false;
    }
    mapping.modifiers.resize(modifierCount);
    for (InputModifierDesc& modifier : mapping.modifiers) {
        if (!ReadModifier(reader, modifier)) {
            return false;
        }
    }

    std::uint32_t triggerCount = 0U;
    if (!reader.ReadUInt32(triggerCount) || triggerCount > InputAssetFormat::MaxStackCount) {
        return false;
    }
    mapping.triggers.resize(triggerCount);
    for (InputTriggerDesc& trigger : mapping.triggers) {
        if (!ReadTrigger(reader, trigger)) {
            return false;
        }
    }
    return true;
}

template <typename T>
InputAssetLoadResult<T> Fail(std::string error) {
    return InputAssetLoadResult<T>{.succeeded = false, .asset = {}, .error = std::move(error)};
}

} // namespace

std::vector<std::uint8_t> EncodeInputAction(const InputActionAsset& asset) {
    std::vector<std::uint8_t> output;
    io::WriteRaw(output, InputAssetFormat::ActionMagic.data(), InputAssetFormat::ActionMagic.size());
    io::WriteUInt32(output, InputAssetFormat::BinaryVersion);
    io::WriteString(output, asset.name);
    io::WriteUInt8(output, static_cast<std::uint8_t>(asset.valueType));
    io::WriteBool(output, asset.consumeInput);
    return output;
}

InputAssetLoadResult<InputActionAsset> DecodeInputAction(std::span<const std::uint8_t> bytes) {
    io::ByteReader reader(std::vector<std::uint8_t>(bytes.begin(), bytes.end()));
    if (!ReadMagic(reader, InputAssetFormat::ActionMagic)) {
        return Fail<InputActionAsset>("Invalid input action magic");
    }
    std::uint32_t version = 0U;
    if (!reader.ReadUInt32(version) || version != InputAssetFormat::BinaryVersion) {
        return Fail<InputActionAsset>("Unsupported input action version");
    }

    InputActionAsset asset;
    std::uint8_t valueType = 0U;
    if (!reader.ReadString(asset.name, InputAssetFormat::MaxNameBytes) ||
        !reader.ReadUInt8(valueType) ||
        valueType > static_cast<std::uint8_t>(InputActionValueType::Axis3D) ||
        !reader.ReadBool(asset.consumeInput)) {
        return Fail<InputActionAsset>("Corrupt input action payload");
    }
    asset.valueType = static_cast<InputActionValueType>(valueType);
    return InputAssetLoadResult<InputActionAsset>{.succeeded = true, .asset = std::move(asset), .error = {}};
}

InputAssetLoadResult<InputActionAsset> ReadInputAction(const std::filesystem::path& path) {
    return DecodeInputAction(io::ReadAllBytes(path));
}

bool WriteInputAction(const std::filesystem::path& path, const InputActionAsset& asset) {
    return io::WriteBytesAtomically(path, EncodeInputAction(asset));
}

std::vector<std::uint8_t> EncodeInputMappingContext(const InputMappingContextAsset& asset) {
    std::vector<std::uint8_t> output;
    io::WriteRaw(output, InputAssetFormat::ContextMagic.data(), InputAssetFormat::ContextMagic.size());
    io::WriteUInt32(output, InputAssetFormat::BinaryVersion);
    io::WriteUInt32(output, static_cast<std::uint32_t>(asset.mappings.size()));
    for (const InputKeyMapping& mapping : asset.mappings) {
        WriteMapping(output, mapping);
    }
    return output;
}

InputAssetLoadResult<InputMappingContextAsset> DecodeInputMappingContext(std::span<const std::uint8_t> bytes) {
    io::ByteReader reader(std::vector<std::uint8_t>(bytes.begin(), bytes.end()));
    if (!ReadMagic(reader, InputAssetFormat::ContextMagic)) {
        return Fail<InputMappingContextAsset>("Invalid mapping context magic");
    }
    std::uint32_t version = 0U;
    if (!reader.ReadUInt32(version) || version != InputAssetFormat::BinaryVersion) {
        return Fail<InputMappingContextAsset>("Unsupported mapping context version");
    }

    std::uint32_t mappingCount = 0U;
    if (!reader.ReadUInt32(mappingCount) || mappingCount > InputAssetFormat::MaxMappingCount) {
        return Fail<InputMappingContextAsset>("Corrupt mapping context payload");
    }

    InputMappingContextAsset asset;
    asset.mappings.resize(mappingCount);
    for (InputKeyMapping& mapping : asset.mappings) {
        if (!ReadMapping(reader, mapping)) {
            return Fail<InputMappingContextAsset>("Corrupt mapping context entry");
        }
    }
    return InputAssetLoadResult<InputMappingContextAsset>{.succeeded = true, .asset = std::move(asset), .error = {}};
}

InputAssetLoadResult<InputMappingContextAsset> ReadInputMappingContext(const std::filesystem::path& path) {
    return DecodeInputMappingContext(io::ReadAllBytes(path));
}

bool WriteInputMappingContext(const std::filesystem::path& path, const InputMappingContextAsset& asset) {
    return io::WriteBytesAtomically(path, EncodeInputMappingContext(asset));
}

} // namespace kb::input
