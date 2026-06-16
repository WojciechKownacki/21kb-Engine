#include "ecs/serialization/ChunkedWorldSnapshotBinaryCodec.hpp"

#include "engine/ecs/ComponentReflection.hpp"
#include "engine/ecs/World.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cstring>
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>

namespace kb::ecs {
namespace {

constexpr std::array<std::byte, 8> kMagic{
    static_cast<std::byte>('K'),
    static_cast<std::byte>('B'),
    static_cast<std::byte>('E'),
    static_cast<std::byte>('C'),
    static_cast<std::byte>('S'),
    static_cast<std::byte>('N'),
    static_cast<std::byte>('P'),
    static_cast<std::byte>('\0'),
};
constexpr std::uint32_t kFormatVersion = 1;
constexpr std::uint32_t kComponentSchemaVersion = 1;
constexpr std::uint64_t kMaxStringBytes = 1U << 20U;

struct DecodedComponentSchema {
    ComponentTypeInfo type;
    std::uint32_t schemaVersion = 0;
};

class ByteReader {
public:
    explicit ByteReader(std::span<const std::byte> source) noexcept
        : source_(source) {}

    [[nodiscard]] bool ReadRaw(void* output, std::size_t size) {
        if (Remaining() < size || (output == nullptr && size != 0U)) {
            return false;
        }
        std::memcpy(output, source_.data() + offset_, size);
        offset_ += size;
        return true;
    }

    [[nodiscard]] bool ReadUInt32(std::uint32_t& output) {
        if (Remaining() < sizeof(std::uint32_t)) {
            return false;
        }
        output = std::to_integer<std::uint32_t>(source_[offset_]) |
            (std::to_integer<std::uint32_t>(source_[offset_ + 1U]) << 8U) |
            (std::to_integer<std::uint32_t>(source_[offset_ + 2U]) << 16U) |
            (std::to_integer<std::uint32_t>(source_[offset_ + 3U]) << 24U);
        offset_ += sizeof(std::uint32_t);
        return true;
    }

    [[nodiscard]] bool ReadUInt64(std::uint64_t& output) {
        if (Remaining() < sizeof(std::uint64_t)) {
            return false;
        }
        output = 0U;
        for (std::uint32_t byte = 0U; byte < 8U; ++byte) {
            output |= std::to_integer<std::uint64_t>(source_[offset_ + byte]) << (byte * 8U);
        }
        offset_ += sizeof(std::uint64_t);
        return true;
    }

    [[nodiscard]] bool ReadSize(std::size_t& output) {
        std::uint64_t value = 0U;
        if (!ReadUInt64(value) || value > static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max())) {
            return false;
        }
        output = static_cast<std::size_t>(value);
        return true;
    }

    [[nodiscard]] bool ReadString(std::string& output) {
        std::uint64_t length = 0U;
        if (!ReadUInt64(length) || length > kMaxStringBytes || length > Remaining()) {
            return false;
        }
        output.assign(reinterpret_cast<const char*>(source_.data() + offset_), static_cast<std::size_t>(length));
        offset_ += static_cast<std::size_t>(length);
        return true;
    }

    [[nodiscard]] bool ReadBytes(std::vector<std::byte>& output) {
        std::size_t size = 0U;
        if (!ReadSize(size) || size > Remaining()) {
            return false;
        }
        output.assign(source_.begin() + static_cast<std::ptrdiff_t>(offset_), source_.begin() + static_cast<std::ptrdiff_t>(offset_ + size));
        offset_ += size;
        return true;
    }

    [[nodiscard]] bool Exhausted() const noexcept {
        return offset_ == source_.size();
    }

    [[nodiscard]] std::size_t Remaining() const noexcept {
        return source_.size() - offset_;
    }

private:
    std::span<const std::byte> source_;
    std::size_t offset_ = 0U;
};

void WriteRaw(std::vector<std::byte>& output, const void* data, std::size_t size) {
    if (size == 0U) {
        return;
    }
    const auto* bytes = static_cast<const std::byte*>(data);
    output.insert(output.end(), bytes, bytes + size);
}

void WriteUInt32(std::vector<std::byte>& output, std::uint32_t value) {
    output.push_back(static_cast<std::byte>(value & 0xFFU));
    output.push_back(static_cast<std::byte>((value >> 8U) & 0xFFU));
    output.push_back(static_cast<std::byte>((value >> 16U) & 0xFFU));
    output.push_back(static_cast<std::byte>((value >> 24U) & 0xFFU));
}

void WriteUInt64(std::vector<std::byte>& output, std::uint64_t value) {
    for (std::uint32_t shift = 0U; shift < 64U; shift += 8U) {
        output.push_back(static_cast<std::byte>((value >> shift) & 0xFFU));
    }
}

void WriteSize(std::vector<std::byte>& output, std::size_t value) {
    WriteUInt64(output, static_cast<std::uint64_t>(value));
}

void WriteString(std::vector<std::byte>& output, std::string_view value) {
    WriteUInt64(output, static_cast<std::uint64_t>(value.size()));
    WriteRaw(output, value.data(), value.size());
}

[[nodiscard]] bool FieldTypeToBinary(ComponentFieldType type, std::uint32_t& output) noexcept {
    switch (type) {
    case ComponentFieldType::Bool:
        output = 1U;
        return true;
    case ComponentFieldType::Int32:
        output = 2U;
        return true;
    case ComponentFieldType::UInt32:
        output = 3U;
        return true;
    case ComponentFieldType::Float32:
        output = 4U;
        return true;
    case ComponentFieldType::Float64:
        output = 5U;
        return true;
    case ComponentFieldType::Vec2Float32:
        output = 6U;
        return true;
    case ComponentFieldType::Vec3Float32:
        output = 7U;
        return true;
    case ComponentFieldType::Vec4Float32:
        output = 8U;
        return true;
    case ComponentFieldType::Enum32:
        output = 9U;
        return true;
    case ComponentFieldType::Bytes:
        output = 10U;
        return true;
    }
    return false;
}

[[nodiscard]] bool FieldTypeFromBinary(std::uint32_t value, ComponentFieldType& output) noexcept {
    switch (value) {
    case 1U:
        output = ComponentFieldType::Bool;
        return true;
    case 2U:
        output = ComponentFieldType::Int32;
        return true;
    case 3U:
        output = ComponentFieldType::UInt32;
        return true;
    case 4U:
        output = ComponentFieldType::Float32;
        return true;
    case 5U:
        output = ComponentFieldType::Float64;
        return true;
    case 6U:
        output = ComponentFieldType::Vec2Float32;
        return true;
    case 7U:
        output = ComponentFieldType::Vec3Float32;
        return true;
    case 8U:
        output = ComponentFieldType::Vec4Float32;
        return true;
    case 9U:
        output = ComponentFieldType::Enum32;
        return true;
    case 10U:
        output = ComponentFieldType::Bytes;
        return true;
    default:
        return false;
    }
}

[[nodiscard]] bool ReadMagic(ByteReader& reader) {
    std::array<std::byte, kMagic.size()> magic{};
    return reader.ReadRaw(magic.data(), magic.size()) && magic == kMagic;
}

[[nodiscard]] bool ComponentDataSizeMatches(std::size_t rowCount, std::size_t componentSize, std::size_t dataSize) noexcept {
    return componentSize != 0U && rowCount <= (std::numeric_limits<std::size_t>::max() / componentSize) && dataSize == rowCount * componentSize;
}

[[nodiscard]] bool WriteSchema(std::vector<std::byte>& output, const World& world, const ComponentTypeInfo& type) {
    WriteUInt64(output, type.id);
    WriteString(output, type.name);
    WriteSize(output, type.size);
    WriteSize(output, type.alignment);
    WriteUInt32(output, kComponentSchemaVersion);

    const ComponentReflection* reflection = world.Reflection(type.id);
    const std::span<const ComponentFieldReflection> fields = reflection != nullptr ? reflection->Fields() : std::span<const ComponentFieldReflection>{};
    WriteSize(output, fields.size());
    for (const ComponentFieldReflection& field : fields) {
        std::uint32_t binaryType = 0U;
        if (!FieldTypeToBinary(field.type, binaryType)) {
            return false;
        }
        WriteString(output, field.name);
        WriteUInt32(output, binaryType);
        WriteSize(output, field.offset);
        WriteSize(output, field.size);
    }
    return true;
}

[[nodiscard]] bool ReadSchema(ByteReader& reader, DecodedComponentSchema& schema) {
    if (!reader.ReadUInt64(schema.type.id) ||
        !reader.ReadString(schema.type.name) ||
        !reader.ReadSize(schema.type.size) ||
        !reader.ReadSize(schema.type.alignment) ||
        !reader.ReadUInt32(schema.schemaVersion) ||
        schema.type.id == 0U ||
        schema.type.size == 0U ||
        schema.schemaVersion != kComponentSchemaVersion) {
        return false;
    }

    std::size_t fieldCount = 0U;
    if (!reader.ReadSize(fieldCount)) {
        return false;
    }

    for (std::size_t index = 0; index < fieldCount; ++index) {
        std::string fieldName;
        std::uint32_t binaryType = 0U;
        std::size_t offset = 0U;
        std::size_t size = 0U;
        ComponentFieldType fieldType = ComponentFieldType::Bytes;
        if (!reader.ReadString(fieldName) ||
            !reader.ReadUInt32(binaryType) ||
            !FieldTypeFromBinary(binaryType, fieldType) ||
            !reader.ReadSize(offset) ||
            !reader.ReadSize(size) ||
            fieldName.empty() ||
            size == 0U ||
            offset > schema.type.size ||
            size > schema.type.size - offset) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] const DecodedComponentSchema* FindSchema(
    const std::unordered_map<ComponentId, DecodedComponentSchema>& schemas,
    ComponentId componentId) noexcept {
    const auto found = schemas.find(componentId);
    return found == schemas.end() ? nullptr : &found->second;
}

} // namespace

bool ChunkedWorldSnapshotBinaryCodec::Encode(const World& world, const ChunkedWorldSnapshot& snapshot, std::vector<std::byte>& output) {
    output.clear();
    output.reserve(64U + snapshot.chunks.size() * 128U);
    output.insert(output.end(), kMagic.begin(), kMagic.end());
    WriteUInt32(output, kFormatVersion);
    WriteSize(output, snapshot.entityCount);

    WriteSize(output, snapshot.componentTypes.size());
    for (const ComponentTypeInfo& type : snapshot.componentTypes) {
        if (type.id == 0U || type.size == 0U || type.name.empty() || !WriteSchema(output, world, type)) {
            output.clear();
            return false;
        }
    }

    WriteSize(output, snapshot.chunks.size());
    for (const ChunkedWorldSnapshotChunk& chunk : snapshot.chunks) {
        WriteSize(output, chunk.archetypeIndex);
        WriteSize(output, chunk.chunkIndex);
        WriteSize(output, chunk.entityIds.size());
        for (Entity::IdType entityId : chunk.entityIds) {
            WriteUInt64(output, entityId);
        }

        WriteSize(output, chunk.components.size());
        for (const ChunkedComponentSnapshot& component : chunk.components) {
            if (component.componentId == 0U || !ComponentDataSizeMatches(chunk.entityIds.size(), component.componentSize, component.data.size())) {
                output.clear();
                return false;
            }
            WriteUInt64(output, component.componentId);
            WriteUInt64(output, component.version);
            WriteSize(output, component.data.size());
            WriteRaw(output, component.data.data(), component.data.size());
        }
    }
    return true;
}

bool ChunkedWorldSnapshotBinaryCodec::Decode(
    std::span<const std::byte> source,
    ChunkedWorldSnapshotHeader& header,
    std::vector<ChunkedWorldSnapshotChunk>& chunks) {
    header = {};
    chunks.clear();

    ByteReader reader{ source };
    std::uint32_t formatVersion = 0U;
    if (!ReadMagic(reader) || !reader.ReadUInt32(formatVersion) || formatVersion != kFormatVersion || !reader.ReadSize(header.entityCount)) {
        return false;
    }

    std::size_t componentCount = 0U;
    if (!reader.ReadSize(componentCount)) {
        return false;
    }

    std::unordered_map<ComponentId, DecodedComponentSchema> schemas;
    schemas.reserve(componentCount);
    header.componentTypes.reserve(componentCount);
    for (std::size_t index = 0; index < componentCount; ++index) {
        DecodedComponentSchema schema;
        if (!ReadSchema(reader, schema) || schemas.find(schema.type.id) != schemas.end()) {
            return false;
        }
        header.componentTypes.push_back(schema.type);
        schemas.emplace(schema.type.id, std::move(schema));
    }

    std::size_t chunkCount = 0U;
    if (!reader.ReadSize(chunkCount)) {
        return false;
    }
    chunks.reserve(chunkCount);

    std::size_t decodedEntities = 0U;
    for (std::size_t chunkIndex = 0; chunkIndex < chunkCount; ++chunkIndex) {
        ChunkedWorldSnapshotChunk chunk;
        if (!reader.ReadSize(chunk.archetypeIndex) || !reader.ReadSize(chunk.chunkIndex)) {
            return false;
        }

        std::size_t entityCount = 0U;
        if (!reader.ReadSize(entityCount)) {
            return false;
        }
        chunk.entityIds.resize(entityCount);
        for (Entity::IdType& entityId : chunk.entityIds) {
            if (!reader.ReadUInt64(entityId) || entityId == 0U) {
                return false;
            }
        }

        std::size_t chunkComponentCount = 0U;
        if (!reader.ReadSize(chunkComponentCount)) {
            return false;
        }
        chunk.components.reserve(chunkComponentCount);
        for (std::size_t componentIndex = 0; componentIndex < chunkComponentCount; ++componentIndex) {
            ChunkedComponentSnapshot component;
            if (!reader.ReadUInt64(component.componentId) || !reader.ReadUInt64(component.version)) {
                return false;
            }

            const DecodedComponentSchema* schema = FindSchema(schemas, component.componentId);
            if (schema == nullptr) {
                return false;
            }

            component.componentName = schema->type.name;
            component.componentSize = schema->type.size;
            if (!reader.ReadBytes(component.data) || !ComponentDataSizeMatches(entityCount, component.componentSize, component.data.size())) {
                return false;
            }
            chunk.components.push_back(std::move(component));
        }

        decodedEntities += entityCount;
        if (decodedEntities > header.entityCount) {
            return false;
        }
        chunks.push_back(std::move(chunk));
    }

    return decodedEntities == header.entityCount && reader.Exhausted();
}

} // namespace kb::ecs
