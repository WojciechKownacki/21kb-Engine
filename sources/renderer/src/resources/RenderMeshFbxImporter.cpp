#include "resources/RenderMeshFbxImporter.hpp"

#include "resources/RenderMeshAssetFinalizer.hpp"

#include <lodepng/lodepng.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iterator>
#include <limits>
#include <string>
#include <vector>

namespace kb::render {
namespace {

constexpr std::array<unsigned char, 23> kBinaryFbxMagic{
    'K', 'a', 'y', 'd', 'a', 'r', 'a', ' ', 'F', 'B', 'X', ' ', 'B', 'i', 'n', 'a', 'r', 'y', ' ', ' ', 0x00, 0x1A, 0x00,
};

struct Vec3 {
    float x = 0.0F;
    float y = 0.0F;
    float z = 0.0F;
};

struct Bounds3 {
    Vec3 min{};
    Vec3 max{};
};

enum class FbxMaterialMapping : std::uint8_t {
    None = 0,
    AllSame,
    ByPolygon,
};

struct FbxGeometryData {
    std::vector<double> vertices;
    std::vector<std::int32_t> polygonVertexIndices;
    std::vector<std::int32_t> materialIndices;
    std::vector<std::string> materialNames;
    FbxMaterialMapping materialMapping = FbxMaterialMapping::None;
};

class FbxReader {
public:
    explicit FbxReader(std::span<const std::byte> bytes) noexcept
        : bytes_(bytes) {}

    [[nodiscard]] bool ReadU8(std::size_t& offset, std::uint8_t& value) const noexcept {
        if (offset >= bytes_.size()) {
            return false;
        }
        value = static_cast<std::uint8_t>(bytes_[offset]);
        ++offset;
        return true;
    }

    template <typename T>
    [[nodiscard]] bool Read(std::size_t& offset, T& value) const noexcept {
        static_assert(std::is_trivially_copyable_v<T>);
        if (offset > bytes_.size() || sizeof(T) > bytes_.size() - offset) {
            return false;
        }
        std::memcpy(&value, bytes_.data() + offset, sizeof(T));
        offset += sizeof(T);
        return true;
    }

    [[nodiscard]] bool ReadBytes(std::size_t& offset, std::size_t size, std::span<const std::byte>& bytes) const noexcept {
        if (offset > bytes_.size() || size > bytes_.size() - offset) {
            return false;
        }
        bytes = bytes_.subspan(offset, size);
        offset += size;
        return true;
    }

    [[nodiscard]] bool StartsWith(std::span<const unsigned char> magic) const noexcept {
        if (bytes_.size() < magic.size()) {
            return false;
        }
        for (std::size_t index = 0; index < magic.size(); ++index) {
            if (static_cast<unsigned char>(bytes_[index]) != magic[index]) {
                return false;
            }
        }
        return true;
    }

    [[nodiscard]] std::size_t Size() const noexcept {
        return bytes_.size();
    }

private:
    std::span<const std::byte> bytes_;
};

[[nodiscard]] std::uint64_t ReadRecordValue32Or64(const FbxReader& reader, std::size_t& offset, bool wideNodes, bool& ok) noexcept {
    if (wideNodes) {
        std::uint64_t value = 0;
        ok = ok && reader.Read(offset, value);
        return value;
    }

    std::uint32_t value = 0;
    ok = ok && reader.Read(offset, value);
    return value;
}

template <typename T>
[[nodiscard]] std::vector<T> DecodeArray(std::span<const std::byte> encoded, std::uint32_t length, std::uint32_t encoding, std::uint32_t encodedByteCount) {
    constexpr std::size_t elementSize = sizeof(T);
    const std::size_t outputBytes = static_cast<std::size_t>(length) * elementSize;
    if (length == 0U || outputBytes / elementSize != length) {
        return {};
    }

    std::vector<std::byte> raw;
    if (encoding == 0U) {
        if (encodedByteCount != outputBytes || encoded.size() < outputBytes) {
            return {};
        }
        raw.assign(encoded.begin(), encoded.begin() + static_cast<std::ptrdiff_t>(outputBytes));
    } else if (encoding == 1U) {
        raw.resize(outputBytes);
        unsigned char* decompressed = nullptr;
        std::size_t decompressedSize = 0U;
        const unsigned result = lodepng_zlib_decompress(
            &decompressed,
            &decompressedSize,
            reinterpret_cast<const unsigned char*>(encoded.data()),
            encodedByteCount,
            &lodepng_default_decompress_settings);
        if (result != 0U || decompressed == nullptr || decompressedSize != outputBytes) {
            std::free(decompressed);
            return {};
        }
        raw.resize(decompressedSize);
        std::ranges::transform(decompressed, decompressed + decompressedSize, raw.begin(), [](unsigned char value) {
            return static_cast<std::byte>(value);
        });
        std::free(decompressed);
    } else {
        return {};
    }

    std::vector<T> values(length);
    std::memcpy(values.data(), raw.data(), outputBytes);
    return values;
}

[[nodiscard]] bool ReadDoubleArrayProperty(const FbxReader& reader, std::size_t& offset, std::vector<double>& values) {
    std::uint32_t length = 0;
    std::uint32_t encoding = 0;
    std::uint32_t encodedByteCount = 0;
    if (!reader.Read(offset, length) || !reader.Read(offset, encoding) || !reader.Read(offset, encodedByteCount)) {
        return false;
    }

    std::span<const std::byte> encoded;
    if (!reader.ReadBytes(offset, encodedByteCount, encoded)) {
        return false;
    }
    values = DecodeArray<double>(encoded, length, encoding, encodedByteCount);
    return !values.empty();
}

[[nodiscard]] bool ReadIntArrayProperty(const FbxReader& reader, std::size_t& offset, std::vector<std::int32_t>& values) {
    std::uint32_t length = 0;
    std::uint32_t encoding = 0;
    std::uint32_t encodedByteCount = 0;
    if (!reader.Read(offset, length) || !reader.Read(offset, encoding) || !reader.Read(offset, encodedByteCount)) {
        return false;
    }

    std::span<const std::byte> encoded;
    if (!reader.ReadBytes(offset, encodedByteCount, encoded)) {
        return false;
    }
    values = DecodeArray<std::int32_t>(encoded, length, encoding, encodedByteCount);
    return !values.empty();
}

[[nodiscard]] bool ReadStringProperty(const FbxReader& reader, std::size_t& offset, std::string& value) {
    std::uint32_t length = 0U;
    if (!reader.Read(offset, length) || length > reader.Size() - offset) {
        return false;
    }
    std::span<const std::byte> bytes;
    if (!reader.ReadBytes(offset, length, bytes)) {
        return false;
    }
    value.assign(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    return true;
}

[[nodiscard]] bool SkipPropertyPayload(const FbxReader& reader, std::size_t& offset, char type) {
    switch (type) {
    case 'Y':
        offset += 2U;
        return offset <= reader.Size();
    case 'C':
        offset += 1U;
        return offset <= reader.Size();
    case 'I':
    case 'F':
        offset += 4U;
        return offset <= reader.Size();
    case 'D':
    case 'L':
        offset += 8U;
        return offset <= reader.Size();
    case 'S':
    case 'R': {
        std::uint32_t size = 0;
        return reader.Read(offset, size) && size <= reader.Size() - offset && (offset += size) <= reader.Size();
    }
    case 'f':
    case 'd':
    case 'i':
    case 'l':
    case 'b': {
        std::uint32_t length = 0;
        std::uint32_t encoding = 0;
        std::uint32_t encodedByteCount = 0;
        static_cast<void>(length);
        static_cast<void>(encoding);
        return reader.Read(offset, length)
            && reader.Read(offset, encoding)
            && reader.Read(offset, encodedByteCount)
            && encodedByteCount <= reader.Size() - offset
            && (offset += encodedByteCount) <= reader.Size();
    }
    default:
        return false;
    }
}

[[nodiscard]] bool ReadTargetProperty(
    const FbxReader& reader,
    std::size_t& offset,
    char type,
    std::string_view nodeName,
    bool inMaterialLayer,
    FbxGeometryData& geometry,
    std::vector<std::string>& stringProperties) {
    if (nodeName == "Vertices" && type == 'd') {
        return ReadDoubleArrayProperty(reader, offset, geometry.vertices);
    }
    if (nodeName == "PolygonVertexIndex" && type == 'i') {
        return ReadIntArrayProperty(reader, offset, geometry.polygonVertexIndices);
    }
    if (nodeName == "Materials" && inMaterialLayer && type == 'i') {
        return ReadIntArrayProperty(reader, offset, geometry.materialIndices);
    }
    if (type == 'S') {
        std::string value;
        if (!ReadStringProperty(reader, offset, value)) {
            return false;
        }
        stringProperties.push_back(std::move(value));
        return true;
    }
    return SkipPropertyPayload(reader, offset, type);
}

[[nodiscard]] std::string MaterialNameFromFbxObjectName(std::string value) {
    constexpr std::string_view prefix = "Material::";
    if (value.starts_with(prefix)) {
        value.erase(0U, prefix.size());
    }
    return value.empty() ? std::string{ "Material" } : value;
}

void CaptureMaterialLayerMapping(const std::string& nodeName, const std::vector<std::string>& stringProperties, FbxGeometryData& geometry) {
    if (stringProperties.empty() || nodeName != "MappingInformationType") {
        return;
    }
    if (stringProperties.front() == "AllSame") {
        geometry.materialMapping = FbxMaterialMapping::AllSame;
    } else if (stringProperties.front() == "ByPolygon") {
        geometry.materialMapping = FbxMaterialMapping::ByPolygon;
    }
}

[[nodiscard]] bool ParseNode(
    const FbxReader& reader,
    std::size_t& offset,
    bool wideNodes,
    FbxGeometryData& geometry,
    bool inMaterialLayer = false) {
    bool ok = true;
    const std::uint64_t endOffset = ReadRecordValue32Or64(reader, offset, wideNodes, ok);
    const std::uint64_t propertyCount = ReadRecordValue32Or64(reader, offset, wideNodes, ok);
    const std::uint64_t propertyListLength = ReadRecordValue32Or64(reader, offset, wideNodes, ok);
    std::uint8_t nameLength = 0;
    ok = ok && reader.ReadU8(offset, nameLength);
    if (!ok) {
        return false;
    }
    if (endOffset == 0U && propertyCount == 0U && propertyListLength == 0U && nameLength == 0U) {
        return true;
    }
    if (endOffset > reader.Size() || endOffset <= offset || nameLength > reader.Size() - offset) {
        return false;
    }

    std::span<const std::byte> nameBytes;
    if (!reader.ReadBytes(offset, nameLength, nameBytes)) {
        return false;
    }
    const std::string nodeName{ reinterpret_cast<const char*>(nameBytes.data()), nameBytes.size() };

    std::vector<std::string> stringProperties;
    stringProperties.reserve(static_cast<std::size_t>(std::min<std::uint64_t>(propertyCount, 4U)));
    for (std::uint64_t propertyIndex = 0; propertyIndex < propertyCount; ++propertyIndex) {
        std::uint8_t type = 0;
        if (!reader.ReadU8(offset, type) ||
            !ReadTargetProperty(reader, offset, static_cast<char>(type), nodeName, inMaterialLayer, geometry, stringProperties)) {
            return false;
        }
    }

    if (nodeName == "Material" && !stringProperties.empty()) {
        geometry.materialNames.push_back(MaterialNameFromFbxObjectName(stringProperties.front()));
    }
    if (inMaterialLayer) {
        CaptureMaterialLayerMapping(nodeName, stringProperties, geometry);
    }

    while (offset < endOffset) {
        const std::size_t before = offset;
        if (!ParseNode(reader, offset, wideNodes, geometry, inMaterialLayer || nodeName == "LayerElementMaterial")) {
            return false;
        }
        if (offset == before) {
            return false;
        }
    }
    offset = static_cast<std::size_t>(endOffset);
    return true;
}

[[nodiscard]] bool ExtractGeometry(std::span<const std::byte> data, FbxGeometryData& geometry) {
    const FbxReader reader{ data };
    if (!reader.StartsWith(std::span<const unsigned char>{ kBinaryFbxMagic.data(), kBinaryFbxMagic.size() }) || reader.Size() < 27U) {
        return false;
    }

    std::size_t offset = 23U;
    std::uint32_t version = 0;
    if (!reader.Read(offset, version)) {
        return false;
    }

    const bool wideNodes = version >= 7500U;
    while (offset < reader.Size()) {
        const std::size_t before = offset;
        if (!ParseNode(reader, offset, wideNodes, geometry)) {
            return false;
        }
        if (offset == before) {
            break;
        }
    }
    return geometry.vertices.size() >= 3U && !geometry.polygonVertexIndices.empty();
}

[[nodiscard]] Vec3 ControlPoint(const std::vector<double>& vertices, std::uint32_t index) noexcept {
    const std::size_t base = static_cast<std::size_t>(index) * 3U;
    return Vec3{
        .x = static_cast<float>(vertices[base]),
        .y = static_cast<float>(vertices[base + 1U]),
        .z = static_cast<float>(vertices[base + 2U]),
    };
}

[[nodiscard]] Bounds3 ComputeControlPointBounds(const std::vector<double>& vertices) noexcept {
    Bounds3 bounds{
        .min = Vec3{
            .x = std::numeric_limits<float>::max(),
            .y = std::numeric_limits<float>::max(),
            .z = std::numeric_limits<float>::max(),
        },
        .max = Vec3{
            .x = std::numeric_limits<float>::lowest(),
            .y = std::numeric_limits<float>::lowest(),
            .z = std::numeric_limits<float>::lowest(),
        },
    };
    const std::uint32_t controlPointCount = static_cast<std::uint32_t>(vertices.size() / 3U);
    for (std::uint32_t index = 0U; index < controlPointCount; ++index) {
        const Vec3 point = ControlPoint(vertices, index);
        bounds.min.x = std::min(bounds.min.x, point.x);
        bounds.min.y = std::min(bounds.min.y, point.y);
        bounds.min.z = std::min(bounds.min.z, point.z);
        bounds.max.x = std::max(bounds.max.x, point.x);
        bounds.max.y = std::max(bounds.max.y, point.y);
        bounds.max.z = std::max(bounds.max.z, point.z);
    }
    return bounds;
}

[[nodiscard]] Vec3 Subtract(Vec3 lhs, Vec3 rhs) noexcept {
    return Vec3{ .x = lhs.x - rhs.x, .y = lhs.y - rhs.y, .z = lhs.z - rhs.z };
}

[[nodiscard]] Vec3 Cross(Vec3 lhs, Vec3 rhs) noexcept {
    return Vec3{
        .x = lhs.y * rhs.z - lhs.z * rhs.y,
        .y = lhs.z * rhs.x - lhs.x * rhs.z,
        .z = lhs.x * rhs.y - lhs.y * rhs.x,
    };
}

[[nodiscard]] Vec3 Normalize(Vec3 value) noexcept {
    const float length = std::sqrt(value.x * value.x + value.y * value.y + value.z * value.z);
    if (length <= std::numeric_limits<float>::epsilon()) {
        return Vec3{ .x = 0.0F, .y = 1.0F, .z = 0.0F };
    }
    return Vec3{ .x = value.x / length, .y = value.y / length, .z = value.z / length };
}

[[nodiscard]] float NormalizeAxis(float value, float min, float max) noexcept {
    const float range = max - min;
    return range > 0.000001F ? (value - min) / range : 0.0F;
}

[[nodiscard]] std::array<float, 2> GeneratePlanarUv(Vec3 position, Vec3 normal, Bounds3 bounds) noexcept {
    const float ax = std::abs(normal.x);
    const float ay = std::abs(normal.y);
    const float az = std::abs(normal.z);

    if (az >= ax && az >= ay) {
        return {
            NormalizeAxis(position.x, bounds.min.x, bounds.max.x),
            NormalizeAxis(position.y, bounds.min.y, bounds.max.y),
        };
    }
    if (ay >= ax && ay >= az) {
        return {
            NormalizeAxis(position.x, bounds.min.x, bounds.max.x),
            NormalizeAxis(position.z, bounds.min.z, bounds.max.z),
        };
    }
    return {
        NormalizeAxis(position.z, bounds.min.z, bounds.max.z),
        NormalizeAxis(position.y, bounds.min.y, bounds.max.y),
    };
}

void AppendTriangle(RenderMeshAssetData& asset, Bounds3 bounds, Vec3 a, Vec3 b, Vec3 c) {
    const Vec3 normal = Normalize(Cross(Subtract(b, a), Subtract(c, a)));
    const std::uint32_t base = static_cast<std::uint32_t>(asset.vertices.size());
    const auto vertex = [bounds, normal](Vec3 position) {
        const std::array<float, 2> uv = GeneratePlanarUv(position, normal, bounds);
        return RenderStaticMeshVertexP3N3UV2{
            .x = position.x,
            .y = position.y,
            .z = position.z,
            .nx = normal.x,
            .ny = normal.y,
            .nz = normal.z,
            .u = uv[0],
            .v = uv[1],
            .u1 = uv[0],
            .v1 = uv[1],
            .r = 1.0F,
            .g = 1.0F,
            .b = 1.0F,
        };
    };
    asset.vertices.push_back(vertex(a));
    asset.vertices.push_back(vertex(b));
    asset.vertices.push_back(vertex(c));
    asset.indices32.push_back(base);
    asset.indices32.push_back(base + 1U);
    asset.indices32.push_back(base + 2U);
}

[[nodiscard]] std::uint32_t MaterialSlotForPolygon(const FbxGeometryData& geometry, std::uint32_t polygonIndex, std::uint32_t slotCount, bool& valid) noexcept {
    valid = true;
    if (geometry.materialIndices.empty() || geometry.materialMapping == FbxMaterialMapping::None) {
        return 0U;
    }

    std::size_t materialIndex = 0U;
    if (geometry.materialMapping == FbxMaterialMapping::ByPolygon) {
        materialIndex = polygonIndex;
    }
    if (materialIndex >= geometry.materialIndices.size()) {
        valid = false;
        return 0U;
    }
    const std::int32_t slot = geometry.materialIndices[materialIndex];
    if (slot < 0 || static_cast<std::uint32_t>(slot) >= slotCount) {
        valid = false;
        return 0U;
    }
    return static_cast<std::uint32_t>(slot);
}

[[nodiscard]] std::optional<RenderMeshAssetData> BuildMesh(const FbxGeometryData& geometry, const RenderMeshFbxImportDesc& desc) {
    if (geometry.vertices.size() % 3U != 0U || geometry.polygonVertexIndices.empty()) {
        return std::nullopt;
    }
    const std::uint32_t controlPointCount = static_cast<std::uint32_t>(geometry.vertices.size() / 3U);

    std::uint32_t slotCount = 1U;
    if (desc.importMaterialSlots) {
        slotCount = std::max<std::uint32_t>(1U, static_cast<std::uint32_t>(geometry.materialNames.size()));
        for (const std::int32_t materialIndex : geometry.materialIndices) {
            if (materialIndex >= 0) {
                slotCount = std::max(slotCount, static_cast<std::uint32_t>(materialIndex) + 1U);
            }
        }
    }
    std::vector<std::vector<std::array<Vec3, 3U>>> trianglesBySlot(slotCount);
    const Bounds3 bounds = ComputeControlPointBounds(geometry.vertices);
    std::vector<std::uint32_t> polygon;
    std::uint32_t polygonIndex = 0U;
    for (std::int32_t rawIndex : geometry.polygonVertexIndices) {
        const bool endsPolygon = rawIndex < 0;
        const std::int32_t decoded = endsPolygon ? -rawIndex - 1 : rawIndex;
        if (decoded < 0 || static_cast<std::uint32_t>(decoded) >= controlPointCount) {
            return std::nullopt;
        }
        polygon.push_back(static_cast<std::uint32_t>(decoded));
        if (!endsPolygon) {
            continue;
        }

        bool materialMappingValid = !desc.importMaterialSlots;
        const std::uint32_t materialSlot = desc.importMaterialSlots
            ? MaterialSlotForPolygon(geometry, polygonIndex, slotCount, materialMappingValid)
            : 0U;
        if (!materialMappingValid) {
            return std::nullopt;
        }
        if (polygon.size() >= 3U) {
            const Vec3 first = ControlPoint(geometry.vertices, polygon[0]);
            for (std::size_t index = 1U; index + 1U < polygon.size(); ++index) {
                trianglesBySlot[materialSlot].push_back({
                    first,
                    ControlPoint(geometry.vertices, polygon[index]),
                    ControlPoint(geometry.vertices, polygon[index + 1U]),
                });
            }
        }
        polygon.clear();
        ++polygonIndex;
    }

    RenderMeshAssetData asset;
    asset.materialSlots.resize(slotCount);
    asset.materialNames.reserve(slotCount);
    for (std::uint32_t slotIndex = 0U; slotIndex < slotCount; ++slotIndex) {
        if (desc.importMaterialSlots && slotIndex < geometry.materialNames.size() && !geometry.materialNames[slotIndex].empty()) {
            asset.materialNames.push_back(geometry.materialNames[slotIndex]);
        } else if (slotCount == 1U && geometry.materialNames.empty()) {
            asset.materialNames.push_back("Default");
        } else {
            asset.materialNames.push_back("Material " + std::to_string(slotIndex + 1U));
        }
    }
    for (std::uint32_t slotIndex = 0U; slotIndex < slotCount; ++slotIndex) {
        const std::uint32_t sectionStart = static_cast<std::uint32_t>(asset.indices32.size());
        for (const std::array<Vec3, 3U>& triangle : trianglesBySlot[slotIndex]) {
            AppendTriangle(asset, bounds, triangle[0], triangle[1], triangle[2]);
        }
        const std::uint32_t sectionIndexCount = static_cast<std::uint32_t>(asset.indices32.size()) - sectionStart;
        if (sectionIndexCount != 0U) {
            asset.sections.push_back(RenderMeshSectionDesc{
                .indexStart = sectionStart,
                .indexCount = sectionIndexCount,
                .materialSlot = slotIndex,
            });
        }
    }

    if (asset.vertices.empty() || asset.indices32.empty() || asset.sections.empty()) {
        return std::nullopt;
    }

    if (!RenderMeshAssetFinalizer::Finalize(asset)) {
        return std::nullopt;
    }
    return asset;
}

} // namespace

std::optional<RenderMeshAssetData> RenderMeshFbxImporter::Load(const std::filesystem::path& path, const RenderMeshFbxImportDesc& desc) {
    std::ifstream input{ path, std::ios::binary };
    if (!input.is_open()) {
        return std::nullopt;
    }
    std::vector<char> bytes{ std::istreambuf_iterator<char>{ input }, std::istreambuf_iterator<char>{} };
    std::vector<std::byte> data(bytes.size());
    std::ranges::transform(bytes, data.begin(), [](char value) {
        return static_cast<std::byte>(static_cast<unsigned char>(value));
    });
    return Load(std::span<const std::byte>{ data.data(), data.size() }, desc);
}

std::optional<RenderMeshAssetData> RenderMeshFbxImporter::Load(std::span<const std::byte> data, const RenderMeshFbxImportDesc& desc) {
    FbxGeometryData geometry;
    if (!ExtractGeometry(data, geometry)) {
        return std::nullopt;
    }
    return BuildMesh(geometry, desc);
}

} // namespace kb::render
