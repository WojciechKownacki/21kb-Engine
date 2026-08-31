#include "kb/render/bake/MeshBaker.hpp"

#include "engine/assets/bake/AssetPack.hpp"

#include <meshoptimizer/src/meshoptimizer.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <limits>
#include <new>
#include <numeric>
#include <stdexcept>
#include <string>
#include <utility>

namespace kb::render::bake {
namespace {

using kb::assets::bake::AssetBakeKey;
using kb::assets::bake::BakedAssetBlock;
using kb::assets::bake::BakedAssetBlockFragment;
using kb::assets::bake::BakedAssetBlockResidency;
using kb::assets::bake::BakedAssetDescriptor;
using kb::assets::bake::BakedAssetSinkStatus;
using kb::assets::bake::BakeTargetProfile;

// Which of the two float vertex layouts the payload carries. Recorded rather than derived from
// the stride so that a future layout of the same width cannot be read as this one.
enum class BakedMeshVertexFormat : std::uint32_t {
    PositionNormalUv = 0U,
    PositionNormalTangentUv = 1U,
};

// Fixed part of the primary block, before the tables. Written out as offsets rather than as a
// struct: this is a file format, and where a compiler puts a member is not.
constexpr std::size_t kPrimaryHeaderBytes = 72U;
constexpr std::size_t kLodEntryBytes = 28U;
constexpr std::size_t kSectionEntryBytes = 40U;
constexpr std::size_t kMeshletEntryBytes = 56U;
constexpr std::size_t kChunkEntryBytes = 28U;
constexpr std::size_t kMaterialSlotEntryBytes = 8U;
constexpr std::uint64_t kMaxMaterialStringBytes = 64ULL * 1024ULL;
constexpr std::uint64_t kMaxMaterialMetadataBytes = 16ULL * 1024ULL * 1024ULL;
constexpr std::uint32_t kMaxMaterialEntries = 65536U;
// The current loader materializes one complete RenderMeshAssetData. Until the runtime consumes
// geometry fragments independently, accepting more decoded geometry than one legal pack block
// defeats the wasm32 block budget and lets a compact hostile catalogue request a near-heap-sized
// allocation before either codec can reject its stream.
constexpr std::uint64_t kMaxResidentGeometryBytes = kb::assets::bake::kMaxAssetPackBlockBytes;

// Fixed part of a geometry chunk, before its cluster table. Everything a chunk says about
// itself is RELATIVE to its own first byte, which is what makes it a self-contained streaming
// fragment: nothing in it has to be rewritten if the chunk moves inside a package.
constexpr std::size_t kChunkHeaderBytes = 24U;
constexpr std::size_t kChunkClusterEntryBytes = 16U;

// Every non-position float reaches the simplifier's attribute metric. UV1, vertex colour and
// tangent discontinuities are just as visible as the normal and UV0 seams; omitting one lets a
// collapse erase detail that the geometric silhouette cannot reveal.
void PutUInt32(std::vector<std::uint8_t>& bytes, std::uint32_t value) {
    for (std::uint32_t shift = 0U; shift < 32U; shift += 8U) {
        bytes.push_back(static_cast<std::uint8_t>((value >> shift) & 0xFFU));
    }
}

void PutUInt64(std::vector<std::uint8_t>& bytes, std::uint64_t value) {
    for (std::uint32_t shift = 0U; shift < 64U; shift += 8U) {
        bytes.push_back(static_cast<std::uint8_t>((value >> shift) & 0xFFU));
    }
}

void PutFloat(std::vector<std::uint8_t>& bytes, float value) {
    PutUInt32(bytes, std::bit_cast<std::uint32_t>(value));
}

[[nodiscard]] std::uint32_t PeekUInt32(std::span<const std::uint8_t> bytes, std::size_t offset) noexcept {
    std::uint32_t value = 0U;
    for (std::uint32_t index = 0U; index < 4U; ++index) {
        value |= static_cast<std::uint32_t>(bytes[offset + index]) << (index * 8U);
    }
    return value;
}

[[nodiscard]] std::uint64_t PeekUInt64(std::span<const std::uint8_t> bytes, std::size_t offset) noexcept {
    std::uint64_t value = 0U;
    for (std::uint32_t index = 0U; index < 8U; ++index) {
        value |= static_cast<std::uint64_t>(bytes[offset + index]) << (index * 8U);
    }
    return value;
}

[[nodiscard]] float PeekFloat(std::span<const std::uint8_t> bytes, std::size_t offset) noexcept {
    return std::bit_cast<float>(PeekUInt32(bytes, offset));
}

void PutString(std::vector<std::uint8_t>& bytes, std::string_view value) {
    PutUInt64(bytes, value.size());
    bytes.insert(bytes.end(), value.begin(), value.end());
}

[[nodiscard]] bool IsValidMaterialDesc(const RenderMaterialDesc& desc) noexcept {
    const std::array<float, 36U> values{
        desc.baseColor[0], desc.baseColor[1], desc.baseColor[2], desc.baseColor[3],
        desc.emissiveColor[0], desc.emissiveColor[1], desc.emissiveColor[2],
        desc.metallicFactor, desc.roughnessFactor, desc.normalScale, desc.occlusionStrength,
        desc.emissiveStrength, desc.alphaCutoff,
        desc.uvTiling[0], desc.uvTiling[1], desc.uvOffset[0], desc.uvOffset[1],
        desc.clearcoatFactor, desc.clearcoatRoughnessFactor,
        desc.sheenColor[0], desc.sheenColor[1], desc.sheenColor[2], desc.sheenRoughnessFactor,
        desc.transmissionFactor, desc.thicknessFactor,
        desc.attenuationColor[0], desc.attenuationColor[1], desc.attenuationColor[2],
        desc.attenuationDistance,
        desc.subsurfaceColor[0], desc.subsurfaceColor[1], desc.subsurfaceColor[2],
        desc.subsurfaceFactor, desc.anisotropyStrength, desc.anisotropyRotation, desc.layerWeight,
    };
    if (!std::ranges::all_of(values, [](float value) noexcept { return std::isfinite(value); }) ||
        static_cast<std::uint32_t>(desc.alphaMode) >
            static_cast<std::uint32_t>(RenderMaterialAlphaMode::Blend) ||
        static_cast<std::uint32_t>(desc.decalBlendMode) >
            static_cast<std::uint32_t>(RenderMaterialDecalBlendMode::Pbr) ||
        static_cast<std::uint32_t>(desc.layerBlendMode) >
            static_cast<std::uint32_t>(RenderMaterialLayerBlendMode::Multiply) ||
        static_cast<std::uint32_t>(desc.translucencyBlend) >
            static_cast<std::uint32_t>(RenderMaterialTranslucencyBlend::AlphaHoldout)) {
        return false;
    }
    // Handles identify one live renderer registry and are not content. The source importers
    // never set them; accepting one would make the source and baked runtime meanings diverge.
    return !desc.albedoTexture.IsValid() && !desc.normalTexture.IsValid() &&
        !desc.metallicRoughnessTexture.IsValid() && !desc.occlusionTexture.IsValid() &&
        !desc.emissiveTexture.IsValid() && !desc.clearcoatTexture.IsValid() &&
        !desc.clearcoatRoughnessTexture.IsValid() && !desc.sheenColorTexture.IsValid() &&
        !desc.transmissionTexture.IsValid() && !desc.thicknessTexture.IsValid() &&
        !desc.anisotropyTexture.IsValid() && !desc.decalTexture.IsValid() &&
        !desc.layerMaskTexture.IsValid();
}

void PutMaterialDesc(std::vector<std::uint8_t>& bytes, const RenderMaterialDesc& desc) {
    for (const float value : desc.baseColor) PutFloat(bytes, value);
    for (const float value : desc.emissiveColor) PutFloat(bytes, value);
    PutFloat(bytes, desc.metallicFactor);
    PutFloat(bytes, desc.roughnessFactor);
    PutFloat(bytes, desc.normalScale);
    PutFloat(bytes, desc.occlusionStrength);
    PutFloat(bytes, desc.emissiveStrength);
    PutFloat(bytes, desc.alphaCutoff);
    for (const float value : desc.uvTiling) PutFloat(bytes, value);
    for (const float value : desc.uvOffset) PutFloat(bytes, value);
    PutFloat(bytes, desc.clearcoatFactor);
    PutFloat(bytes, desc.clearcoatRoughnessFactor);
    for (const float value : desc.sheenColor) PutFloat(bytes, value);
    PutFloat(bytes, desc.sheenRoughnessFactor);
    PutFloat(bytes, desc.transmissionFactor);
    PutFloat(bytes, desc.thicknessFactor);
    for (const float value : desc.attenuationColor) PutFloat(bytes, value);
    PutFloat(bytes, desc.attenuationDistance);
    for (const float value : desc.subsurfaceColor) PutFloat(bytes, value);
    PutFloat(bytes, desc.subsurfaceFactor);
    PutFloat(bytes, desc.anisotropyStrength);
    PutFloat(bytes, desc.anisotropyRotation);
    PutFloat(bytes, desc.layerWeight);
    PutUInt32(bytes, static_cast<std::uint32_t>(desc.alphaMode));
    PutUInt32(bytes, static_cast<std::uint32_t>(desc.decalBlendMode));
    PutUInt32(bytes, static_cast<std::uint32_t>(desc.layerBlendMode));
    PutUInt32(bytes, static_cast<std::uint32_t>(desc.translucencyBlend));
    PutUInt32(bytes, desc.doubleSided ? 1U : 0U);
    PutUInt32(bytes, desc.writesDepth ? 1U : 0U);
    PutUInt64(bytes, desc.albedoTextureAssetId);
    PutUInt64(bytes, desc.normalTextureAssetId);
    PutUInt64(bytes, desc.metallicRoughnessTextureAssetId);
    PutUInt64(bytes, desc.occlusionTextureAssetId);
    PutUInt64(bytes, desc.emissiveTextureAssetId);
    PutUInt64(bytes, desc.clearcoatTextureAssetId);
    PutUInt64(bytes, desc.clearcoatRoughnessTextureAssetId);
    PutUInt64(bytes, desc.sheenColorTextureAssetId);
    PutUInt64(bytes, desc.transmissionTextureAssetId);
    PutUInt64(bytes, desc.thicknessTextureAssetId);
    PutUInt64(bytes, desc.anisotropyTextureAssetId);
    PutUInt64(bytes, desc.decalTextureAssetId);
    PutUInt64(bytes, desc.layerMaskTextureAssetId);
}

[[nodiscard]] std::array<const std::string*, 13U> MaterialPaths(
    const RenderMeshEmbeddedMaterial& material) noexcept {
    return {
        &material.albedoTexturePath,
        &material.normalTexturePath,
        &material.metallicRoughnessTexturePath,
        &material.occlusionTexturePath,
        &material.emissiveTexturePath,
        &material.clearcoatTexturePath,
        &material.clearcoatRoughnessTexturePath,
        &material.sheenColorTexturePath,
        &material.transmissionTexturePath,
        &material.thicknessTexturePath,
        &material.anisotropyTexturePath,
        &material.decalTexturePath,
        &material.layerMaskTexturePath,
    };
}

[[nodiscard]] std::vector<std::uint8_t> SerializeMaterialMetadata(const RenderMeshAssetData& source) {
    if (source.materialNames.empty() && source.embeddedMaterials.empty()) {
        return {};
    }
    std::vector<std::uint8_t> bytes;
    PutUInt64(bytes, source.materialNames.size());
    PutUInt64(bytes, source.embeddedMaterials.size());
    for (const std::string& name : source.materialNames) {
        PutString(bytes, name);
    }
    for (const RenderMeshEmbeddedMaterial& material : source.embeddedMaterials) {
        PutString(bytes, material.name);
        PutMaterialDesc(bytes, material.desc);
        for (const std::string* path : MaterialPaths(material)) {
            PutString(bytes, *path);
        }
    }
    return bytes;
}

[[nodiscard]] bool IsValidSourceMaterials(
    const RenderMeshAssetData& source,
    std::span<const std::uint8_t> metadata) noexcept {
    if (source.materialSlots.size() > kMaxMaterialEntries ||
        (!source.materialNames.empty() && source.materialNames.size() != source.materialSlots.size()) ||
        (!source.embeddedMaterials.empty() && source.embeddedMaterials.size() != source.materialSlots.size()) ||
        metadata.size() > kMaxMaterialMetadataBytes) {
        return false;
    }
    const auto validString = [](const std::string& value) noexcept {
        return value.size() <= kMaxMaterialStringBytes;
    };
    if (!std::ranges::all_of(source.materialNames, validString)) {
        return false;
    }
    for (const RenderMeshEmbeddedMaterial& material : source.embeddedMaterials) {
        if (!validString(material.name) || !IsValidMaterialDesc(material.desc)) {
            return false;
        }
        for (const std::string* path : MaterialPaths(material)) {
            if (!validString(*path)) {
                return false;
            }
        }
    }
    return true;
}

class ByteReader {
public:
    explicit ByteReader(std::span<const std::uint8_t> bytes) noexcept
        : bytes_{ bytes } {}

    [[nodiscard]] bool ReadUInt32(std::uint32_t& value) noexcept {
        if (Remaining() < sizeof(std::uint32_t)) return false;
        value = PeekUInt32(bytes_, cursor_);
        cursor_ += sizeof(std::uint32_t);
        return true;
    }

    [[nodiscard]] bool ReadUInt64(std::uint64_t& value) noexcept {
        if (Remaining() < sizeof(std::uint64_t)) return false;
        value = PeekUInt64(bytes_, cursor_);
        cursor_ += sizeof(std::uint64_t);
        return true;
    }

    [[nodiscard]] bool ReadFloat(float& value) noexcept {
        std::uint32_t bits = 0U;
        if (!ReadUInt32(bits)) return false;
        value = std::bit_cast<float>(bits);
        return true;
    }

    [[nodiscard]] bool ReadString(std::string& value) {
        std::uint64_t length = 0U;
        if (!ReadUInt64(length) || length > kMaxMaterialStringBytes || length > Remaining()) return false;
        const char* const begin = reinterpret_cast<const char*>(bytes_.data() + cursor_);
        value.assign(begin, begin + static_cast<std::size_t>(length));
        cursor_ += static_cast<std::size_t>(length);
        return true;
    }

    [[nodiscard]] bool Empty() const noexcept { return cursor_ == bytes_.size(); }

private:
    [[nodiscard]] std::size_t Remaining() const noexcept { return bytes_.size() - cursor_; }

    std::span<const std::uint8_t> bytes_;
    std::size_t cursor_ = 0U;
};

[[nodiscard]] bool ReadMaterialDesc(ByteReader& reader, RenderMaterialDesc& desc) {
    for (float& value : desc.baseColor) if (!reader.ReadFloat(value)) return false;
    for (float& value : desc.emissiveColor) if (!reader.ReadFloat(value)) return false;
    if (!reader.ReadFloat(desc.metallicFactor) || !reader.ReadFloat(desc.roughnessFactor) ||
        !reader.ReadFloat(desc.normalScale) || !reader.ReadFloat(desc.occlusionStrength) ||
        !reader.ReadFloat(desc.emissiveStrength) || !reader.ReadFloat(desc.alphaCutoff)) return false;
    for (float& value : desc.uvTiling) if (!reader.ReadFloat(value)) return false;
    for (float& value : desc.uvOffset) if (!reader.ReadFloat(value)) return false;
    if (!reader.ReadFloat(desc.clearcoatFactor) || !reader.ReadFloat(desc.clearcoatRoughnessFactor)) return false;
    for (float& value : desc.sheenColor) if (!reader.ReadFloat(value)) return false;
    if (!reader.ReadFloat(desc.sheenRoughnessFactor) || !reader.ReadFloat(desc.transmissionFactor) ||
        !reader.ReadFloat(desc.thicknessFactor)) return false;
    for (float& value : desc.attenuationColor) if (!reader.ReadFloat(value)) return false;
    if (!reader.ReadFloat(desc.attenuationDistance)) return false;
    for (float& value : desc.subsurfaceColor) if (!reader.ReadFloat(value)) return false;
    if (!reader.ReadFloat(desc.subsurfaceFactor) || !reader.ReadFloat(desc.anisotropyStrength) ||
        !reader.ReadFloat(desc.anisotropyRotation) || !reader.ReadFloat(desc.layerWeight)) return false;

    std::uint32_t alphaMode = 0U;
    std::uint32_t decalBlendMode = 0U;
    std::uint32_t layerBlendMode = 0U;
    std::uint32_t translucencyBlend = 0U;
    std::uint32_t doubleSided = 0U;
    std::uint32_t writesDepth = 0U;
    if (!reader.ReadUInt32(alphaMode) || !reader.ReadUInt32(decalBlendMode) ||
        !reader.ReadUInt32(layerBlendMode) || !reader.ReadUInt32(translucencyBlend) ||
        !reader.ReadUInt32(doubleSided) || !reader.ReadUInt32(writesDepth) ||
        doubleSided > 1U || writesDepth > 1U) return false;
    desc.alphaMode = static_cast<RenderMaterialAlphaMode>(alphaMode);
    desc.decalBlendMode = static_cast<RenderMaterialDecalBlendMode>(decalBlendMode);
    desc.layerBlendMode = static_cast<RenderMaterialLayerBlendMode>(layerBlendMode);
    desc.translucencyBlend = static_cast<RenderMaterialTranslucencyBlend>(translucencyBlend);
    desc.doubleSided = doubleSided != 0U;
    desc.writesDepth = writesDepth != 0U;
    if (!reader.ReadUInt64(desc.albedoTextureAssetId) || !reader.ReadUInt64(desc.normalTextureAssetId) ||
        !reader.ReadUInt64(desc.metallicRoughnessTextureAssetId) ||
        !reader.ReadUInt64(desc.occlusionTextureAssetId) || !reader.ReadUInt64(desc.emissiveTextureAssetId) ||
        !reader.ReadUInt64(desc.clearcoatTextureAssetId) ||
        !reader.ReadUInt64(desc.clearcoatRoughnessTextureAssetId) ||
        !reader.ReadUInt64(desc.sheenColorTextureAssetId) || !reader.ReadUInt64(desc.transmissionTextureAssetId) ||
        !reader.ReadUInt64(desc.thicknessTextureAssetId) || !reader.ReadUInt64(desc.anisotropyTextureAssetId) ||
        !reader.ReadUInt64(desc.decalTextureAssetId) || !reader.ReadUInt64(desc.layerMaskTextureAssetId)) return false;
    return IsValidMaterialDesc(desc);
}

[[nodiscard]] bool ReadMaterialMetadata(
    std::span<const std::uint8_t> bytes,
    std::uint32_t materialSlotCount,
    std::vector<std::string>& materialNames,
    std::vector<RenderMeshEmbeddedMaterial>& embeddedMaterials) {
    if (bytes.empty()) return true;
    if (bytes.size() > kMaxMaterialMetadataBytes) return false;
    ByteReader reader{ bytes };
    std::uint64_t materialNameCount = 0U;
    std::uint64_t embeddedMaterialCount = 0U;
    if (!reader.ReadUInt64(materialNameCount) || !reader.ReadUInt64(embeddedMaterialCount) ||
        (materialNameCount == 0U && embeddedMaterialCount == 0U) ||
        materialNameCount > kMaxMaterialEntries || embeddedMaterialCount > kMaxMaterialEntries ||
        (materialNameCount != 0U && materialNameCount != materialSlotCount) ||
        (embeddedMaterialCount != 0U && embeddedMaterialCount != materialSlotCount)) {
        return false;
    }
    materialNames.reserve(static_cast<std::size_t>(materialNameCount));
    for (std::uint64_t index = 0U; index < materialNameCount; ++index) {
        std::string name;
        if (!reader.ReadString(name)) return false;
        materialNames.push_back(std::move(name));
    }
    embeddedMaterials.reserve(static_cast<std::size_t>(embeddedMaterialCount));
    for (std::uint64_t index = 0U; index < embeddedMaterialCount; ++index) {
        RenderMeshEmbeddedMaterial material{};
        if (!reader.ReadString(material.name) || !ReadMaterialDesc(reader, material.desc)) return false;
        const std::array<std::string*, 13U> paths{
            &material.albedoTexturePath,
            &material.normalTexturePath,
            &material.metallicRoughnessTexturePath,
            &material.occlusionTexturePath,
            &material.emissiveTexturePath,
            &material.clearcoatTexturePath,
            &material.clearcoatRoughnessTexturePath,
            &material.sheenColorTexturePath,
            &material.transmissionTexturePath,
            &material.thicknessTexturePath,
            &material.anisotropyTexturePath,
            &material.decalTexturePath,
            &material.layerMaskTexturePath,
        };
        for (std::string* path : paths) if (!reader.ReadString(*path)) return false;
        embeddedMaterials.push_back(std::move(material));
    }
    return reader.Empty();
}

[[nodiscard]] bool IsFiniteBounds(const RenderBoundsSphere& bounds) noexcept {
    return bounds.radius > 0.0F && std::isfinite(bounds.radius) &&
        std::ranges::all_of(bounds.center, [](float value) noexcept { return std::isfinite(value); });
}

[[nodiscard]] bool IsFiniteCone(const std::array<float, 4>& cone) noexcept {
    if (!std::ranges::all_of(cone, [](float value) noexcept { return std::isfinite(value); }) ||
        cone[3] < 0.0F || cone[3] > 1.0F) {
        return false;
    }
    if (cone[3] == 1.0F) {
        return true;
    }
    const float axisLengthSquared = cone[0] * cone[0] + cone[1] * cone[1] + cone[2] * cone[2];
    return axisLengthSquared >= 0.98F && axisLengthSquared <= 1.02F;
}

[[nodiscard]] bool NearlyEqual(float lhs, float rhs) noexcept {
    if (!std::isfinite(lhs) || !std::isfinite(rhs)) {
        return false;
    }
    const float scale = std::max({ 1.0F, std::fabs(lhs), std::fabs(rhs) });
    return std::fabs(lhs - rhs) <= scale * 1.0e-5F;
}

[[nodiscard]] bool BoundsMatch(const RenderBoundsSphere& lhs, const RenderBoundsSphere& rhs) noexcept {
    return NearlyEqual(lhs.center[0], rhs.center[0]) && NearlyEqual(lhs.center[1], rhs.center[1]) &&
        NearlyEqual(lhs.center[2], rhs.center[2]) && NearlyEqual(lhs.radius, rhs.radius);
}

[[nodiscard]] bool ConeMatches(const std::array<float, 4>& lhs, const meshopt_Bounds& rhs) noexcept {
    return NearlyEqual(lhs[0], rhs.cone_axis[0]) && NearlyEqual(lhs[1], rhs.cone_axis[1]) &&
        NearlyEqual(lhs[2], rhs.cone_axis[2]) && NearlyEqual(lhs[3], rhs.cone_cutoff);
}

// One material's worth of triangles at one level of detail, as an index list of its own. The
// LOD chain simplifies these separately, which is what keeps a material boundary a boundary.
struct LodSection {
    std::vector<std::uint32_t> indices;
    std::uint32_t materialSlot = 0U;
};

struct LodLevel {
    std::vector<LodSection> sections;
    // Absolute, in the units the positions are in, and accumulated down the chain. Camera
    // independent by construction: no viewport and no field of view is read anywhere here.
    float absoluteError = 0.0F;
};

// The source, unpacked once into the shapes meshoptimizer wants.
struct SourceGeometry {
    const std::uint8_t* vertexBytes = nullptr;
    const float* positions = nullptr;
    std::uint32_t vertexStride = 0U;
    std::uint32_t vertexCount = 0U;
    BakedMeshVertexFormat format = BakedMeshVertexFormat::PositionNormalUv;
    // Every non-position float, contiguous, for the attribute metric.
    std::vector<float> attributes;
    std::vector<float> attributeWeights;
    std::uint32_t attributeCount = 0U;
    std::vector<std::uint32_t> indices;
    std::vector<LodSection> sections;
};

// One geometry chunk: a run of whole clusters, and therefore a run of whole triangles and a
// contiguous slice of both output buffers. A cluster never straddles one.
struct GeometryChunk {
    std::uint32_t firstMeshlet = 0U;
    std::uint32_t meshletCount = 0U;
    std::uint32_t vertexStart = 0U;
    std::uint32_t vertexCount = 0U;
    std::uint32_t indexStart = 0U;
    std::uint32_t indexCount = 0U;
};

struct ClusterGroup {
    std::uint32_t firstMeshlet = 0U;
    std::uint32_t meshletCount = 0U;
};

[[nodiscard]] bool SourceUsesTangents(const RenderMeshAssetData& source) noexcept {
    return !source.tangentVertices.empty();
}

[[nodiscard]] RenderBoundsSphere BoundsOfVertices(
    const std::uint8_t* vertices,
    std::uint32_t vertexStride,
    std::uint32_t first,
    std::uint32_t count) noexcept;

// Populates `out` from the asset, or says which rule the asset breaks. Nothing here is a
// tolerance: a mesh that is not whole triangles over vertices that exist is not a mesh whose
// clusters mean anything.
[[nodiscard]] MeshBakeStatus ReadSourceGeometry(const RenderMeshAssetData& source, SourceGeometry& out) {
    const bool tangents = SourceUsesTangents(source);
    const std::size_t vertexCount = tangents ? source.tangentVertices.size() : source.vertices.size();
    if (vertexCount == 0U || source.sections.empty()) {
        return MeshBakeStatus::EmptySource;
    }
    if (vertexCount > std::numeric_limits<std::uint32_t>::max()) {
        return MeshBakeStatus::EmptySource;
    }
    if (!source.indices16.empty() && !source.indices32.empty()) {
        // RefreshDesc renders indices16 when both are populated. Choosing indices32 here would
        // bake different triangles from the same RenderMeshAssetData than the source path draws.
        return MeshBakeStatus::MalformedIndices;
    }

    out.format = tangents ? BakedMeshVertexFormat::PositionNormalTangentUv : BakedMeshVertexFormat::PositionNormalUv;
    out.vertexStride = static_cast<std::uint32_t>(
        tangents ? sizeof(RenderStaticMeshVertexP3N3T4UV2) : sizeof(RenderStaticMeshVertexP3N3UV2));
    out.vertexCount = static_cast<std::uint32_t>(vertexCount);
    out.vertexBytes = tangents ? reinterpret_cast<const std::uint8_t*>(source.tangentVertices.data())
                               : reinterpret_cast<const std::uint8_t*>(source.vertices.data());
    out.positions = reinterpret_cast<const float*>(out.vertexBytes);

    if (!source.indices32.empty()) {
        out.indices = source.indices32;
    } else {
        out.indices.assign(source.indices16.begin(), source.indices16.end());
    }
    if (out.indices.empty()) {
        return MeshBakeStatus::EmptySource;
    }
    if (out.indices.size() % 3U != 0U) {
        return MeshBakeStatus::MalformedIndices;
    }
    // The clusterizer and the simplifier both do arithmetic on positions, and a NaN there does
    // not fail: it spreads into every bound and every cone as a value that compares false
    // against everything.
    const std::size_t floatCount = vertexCount * (out.vertexStride / sizeof(float));
    for (std::size_t index = 0U; index < floatCount; ++index) {
        if (!std::isfinite(out.positions[index])) {
            return MeshBakeStatus::NonFiniteGeometry;
        }
    }
    const RenderBoundsSphere sourceBounds = BoundsOfVertices(
        out.vertexBytes, out.vertexStride, 0U, out.vertexCount);
    if (!std::ranges::all_of(sourceBounds.center, [](float value) noexcept { return std::isfinite(value); }) ||
        !std::isfinite(sourceBounds.radius)) {
        return MeshBakeStatus::NonFiniteGeometry;
    }

    // The two supported layouts are all-float and have no padding. Positions occupy the first
    // three floats; every remaining channel is copied in declaration order.
    const std::size_t vertexFloats = out.vertexStride / sizeof(float);
    out.attributeCount = static_cast<std::uint32_t>(vertexFloats - 3U);
    out.attributes.resize(vertexCount * out.attributeCount);
    for (std::size_t vertex = 0U; vertex < vertexCount; ++vertex) {
        const float* const vertexFloatsBase = out.positions + vertex * vertexFloats;
        std::copy_n(vertexFloatsBase + 3U, out.attributeCount, out.attributes.data() + vertex * out.attributeCount);
    }
    out.attributeWeights = { kSimplifyNormalWeight, kSimplifyNormalWeight, kSimplifyNormalWeight };
    if (tangents) {
        out.attributeWeights.insert(out.attributeWeights.end(), 4U, kSimplifyTangentWeight);
    }
    out.attributeWeights.insert(out.attributeWeights.end(), 4U, kSimplifyUvWeight);
    out.attributeWeights.insert(out.attributeWeights.end(), 4U, kSimplifyColorWeight);
    if (out.attributeWeights.size() != out.attributeCount) {
        return MeshBakeStatus::UnsupportedSourceShape;
    }
    // meshoptimizer's attribute quadrics square weighted values and gradients in float. Finite
    // endpoints alone are insufficient: +/-FLT_MAX has an infinite difference and poisons the
    // collapse ranking. Keep ample headroom for summing every channel; the post-call checks in
    // BuildLodChain remain the final guard for pathological geometry gradients.
    const double safeWeightedAttribute = std::sqrt(static_cast<double>(std::numeric_limits<float>::max())) /
        (static_cast<double>(out.attributeCount) * 16.0);
    for (std::size_t vertex = 0U; vertex < vertexCount; ++vertex) {
        for (std::uint32_t attribute = 0U; attribute < out.attributeCount; ++attribute) {
            const double weighted = static_cast<double>(out.attributes[vertex * out.attributeCount + attribute]) *
                out.attributeWeights[attribute];
            if (std::fabs(weighted) > safeWeightedAttribute) {
                return MeshBakeStatus::NonFiniteGeometry;
            }
        }
    }

    std::vector<bool> covered(out.indices.size(), false);
    out.sections.reserve(source.sections.size());
    for (const RenderMeshSectionDesc& section : source.sections) {
        if (section.lodLevel != 0U || section.terrainLayerIndex != UINT8_MAX) {
            return MeshBakeStatus::UnsupportedSourceShape;
        }
        const std::uint32_t sectionVertexCount = section.vertexStart < out.vertexCount
            ? (section.vertexCount == 0U ? out.vertexCount - section.vertexStart : section.vertexCount)
            : 0U;
        if (section.indexCount == 0U || section.indexCount % 3U != 0U ||
            section.indexStart > out.indices.size() ||
            section.indexCount > out.indices.size() - section.indexStart ||
            sectionVertexCount == 0U || sectionVertexCount > out.vertexCount - section.vertexStart ||
            (source.materialSlots.empty() ? section.materialSlot != 0U
                                          : section.materialSlot >= source.materialSlots.size())) {
            return MeshBakeStatus::MalformedSections;
        }
        for (std::uint32_t offset = 0U; offset < section.indexCount; ++offset) {
            // Two sections over one triangle would put that triangle in two clusters with two
            // materials, and the chunk table would then describe overlapping geometry.
            if (covered[section.indexStart + offset]) {
                return MeshBakeStatus::MalformedSections;
            }
            covered[section.indexStart + offset] = true;
            std::uint32_t& index = out.indices[section.indexStart + offset];
            if (index >= sectionVertexCount) {
                return MeshBakeStatus::MalformedIndices;
            }
            index += section.vertexStart;
        }
        LodSection lodSection{};
        lodSection.materialSlot = section.materialSlot;
        lodSection.indices.assign(
            out.indices.begin() + static_cast<std::ptrdiff_t>(section.indexStart),
            out.indices.begin() + static_cast<std::ptrdiff_t>(section.indexStart + section.indexCount));
        out.sections.push_back(std::move(lodSection));
    }
    if (!std::ranges::all_of(covered, [](bool value) noexcept { return value; })) {
        return MeshBakeStatus::MalformedSections;
    }
    return MeshBakeStatus::Success;
}

// The whole LOD chain, simplified level from level. Stops as soon as a level fails to get
// smaller: a level that is not smaller than the one above it is not a level, and emitting it
// would be a chain that claims a reduction it did not make.
[[nodiscard]] MeshBakeStatus BuildLodChain(const SourceGeometry& source, std::vector<LodLevel>& lods) {
    lods.clear();
    lods.push_back(LodLevel{ .sections = source.sections, .absoluteError = 0.0F });

    const float scale = meshopt_simplifyScale(source.positions, source.vertexCount, source.vertexStride);
    const float stepErrorLimit = kLodErrorFractionOfScale * scale;
    if (!std::isfinite(scale) || !std::isfinite(stepErrorLimit)) {
        return MeshBakeStatus::NonFiniteGeometry;
    }
    while (lods.size() < kMaxLodCount) {
        const LodLevel& previous = lods.back();
        std::size_t previousIndices = 0U;
        for (const LodSection& section : previous.sections) {
            previousIndices += section.indices.size();
        }

        LodLevel next{};
        next.sections.reserve(previous.sections.size());
        std::size_t nextIndices = 0U;
        float stepError = 0.0F;
        for (const LodSection& section : previous.sections) {
            const std::size_t targetTriangles =
                static_cast<std::size_t>(static_cast<float>(section.indices.size() / 3U) * kLodTriangleRatio);
            const std::size_t targetIndices = std::max<std::size_t>(targetTriangles, 1U) * 3U;
            LodSection simplified{};
            simplified.materialSlot = section.materialSlot;
            simplified.indices.resize(section.indices.size());
            float sectionError = 0.0F;
            // LockBorder is what keeps two materials welded: a section is simplified without
            // its neighbours, so a vertex on the seam that moves leaves a crack no later pass
            // can close. ErrorAbsolute is the binding half -- the returned error is then in the
            // mesh's own units and owes nothing to a camera.
            const std::size_t produced = meshopt_simplifyWithAttributes(
                simplified.indices.data(),
                section.indices.data(),
                section.indices.size(),
                source.positions,
                source.vertexCount,
                source.vertexStride,
                source.attributes.data(),
                source.attributeCount * sizeof(float),
                source.attributeWeights.data(),
                source.attributeCount,
                nullptr,
                std::min(targetIndices, section.indices.size()),
                stepErrorLimit,
                meshopt_SimplifyErrorAbsolute | meshopt_SimplifyLockBorder,
                &sectionError);
            if (!std::isfinite(sectionError)) {
                return MeshBakeStatus::NonFiniteGeometry;
            }
            if (produced > section.indices.size() || produced % 3U != 0U ||
                !std::ranges::all_of(
                    std::span<const std::uint32_t>{ simplified.indices.data(), produced },
                    [&source](std::uint32_t index) noexcept { return index < source.vertexCount; })) {
                return MeshBakeStatus::SimplificationFailed;
            }
            simplified.indices.resize(produced);
            if (produced < 3U) {
                // A section that simplified itself out of existence would leave the level
                // without a material it had; the chain stops instead.
                return MeshBakeStatus::Success;
            }
            nextIndices += produced;
            stepError = std::max(stepError, sectionError);
            next.sections.push_back(std::move(simplified));
        }
        if (nextIndices >= previousIndices) {
            return MeshBakeStatus::Success;
        }
        // Summed, not maxed: the level below was built from an already approximate mesh, so its
        // deviation from the ORIGINAL is at most the deviations of every step taken to reach it.
        next.absoluteError = previous.absoluteError + stepError;
        if (!std::isfinite(next.absoluteError)) {
            return MeshBakeStatus::NonFiniteGeometry;
        }
        lods.push_back(std::move(next));
    }
    return MeshBakeStatus::Success;
}

// Axis-aligned box of a run of the output vertex buffer. This is what a streaming fragment
// records: a box, not a sphere, because a load priority is computed against the page's extent
// and a sphere around a long thin page overstates it in every direction.
struct VertexAabb {
    std::array<float, 3> low{};
    std::array<float, 3> high{};
};

[[nodiscard]] VertexAabb AabbOfVertices(
    const std::uint8_t* vertices,
    std::uint32_t vertexStride,
    std::uint32_t first,
    std::uint32_t count) noexcept {
    VertexAabb box{};
    for (std::uint32_t vertex = 0U; vertex < count; ++vertex) {
        std::array<float, 3> position{};
        std::memcpy(
            position.data(), vertices + static_cast<std::size_t>(first + vertex) * vertexStride, sizeof(position));
        for (std::size_t axis = 0U; axis < 3U; ++axis) {
            box.low[axis] = vertex == 0U ? position[axis] : std::min(box.low[axis], position[axis]);
            box.high[axis] = vertex == 0U ? position[axis] : std::max(box.high[axis], position[axis]);
        }
    }
    return box;
}

[[nodiscard]] RenderBoundsSphere BoundsOfVertices(
    const std::uint8_t* vertices,
    std::uint32_t vertexStride,
    std::uint32_t first,
    std::uint32_t count) noexcept {
    if (count == 0U) {
        return RenderBoundsSphere{};
    }
    const VertexAabb box = AabbOfVertices(vertices, vertexStride, first, count);
    const std::array<float, 3> center{
        static_cast<float>((static_cast<double>(box.low[0]) + box.high[0]) * 0.5),
        static_cast<float>((static_cast<double>(box.low[1]) + box.high[1]) * 0.5),
        static_cast<float>((static_cast<double>(box.low[2]) + box.high[2]) * 0.5),
    };
    double radiusSquared = 0.0;
    for (std::uint32_t vertex = 0U; vertex < count; ++vertex) {
        std::array<float, 3> position{};
        std::memcpy(
            position.data(), vertices + static_cast<std::size_t>(first + vertex) * vertexStride, sizeof(position));
        const double dx = static_cast<double>(position[0]) - center[0];
        const double dy = static_cast<double>(position[1]) - center[1];
        const double dz = static_cast<double>(position[2]) - center[2];
        radiusSquared = std::max(radiusSquared, dx * dx + dy * dy + dz * dz);
    }
    return RenderBoundsSphere{ .center = center, .radius = static_cast<float>(std::sqrt(radiusSquared)) };
}

// Everything the primary block describes, built once from the LOD chain.
struct ClusteredMesh {
    // Both supported layouts are padding-free runs of floats. Owning them as floats keeps the
    // object lifetime and alignment required by meshoptimizer explicit; the codec may still
    // inspect their object representation through bytes.
    std::vector<float> vertices;
    std::vector<std::uint32_t> indices;
    std::vector<RenderMeshSectionDesc> sections;
    std::vector<RenderMeshletDesc> meshlets;
    std::vector<RenderMeshLodDesc> lods;
    std::vector<float> lodErrors;
    std::vector<ClusterGroup> groups;
};

[[nodiscard]] std::uint64_t EncodedChunkByteBound(
    std::uint32_t meshletCount,
    std::uint32_t vertexCount,
    std::uint32_t indexCount,
    std::uint32_t vertexStride) noexcept {
    return kChunkHeaderBytes + static_cast<std::uint64_t>(meshletCount) * kChunkClusterEntryBytes +
        meshopt_encodeVertexBufferBound(vertexCount, vertexStride) +
        meshopt_encodeIndexBufferBound(indexCount, vertexCount);
}

[[nodiscard]] std::uint32_t TargetClusterGroupSize(
    std::uint32_t vertexStride,
    std::uint64_t maxChunkBytes) noexcept {
    for (std::uint32_t target = kTargetClusterGroupSize; target > 1U; --target) {
        const std::uint32_t largestPartition = target + target / 3U;
        if (EncodedChunkByteBound(
                largestPartition,
                largestPartition * kMaxClusterVertices,
                largestPartition * kMaxClusterTriangles * 3U,
                vertexStride) <= maxChunkBytes) {
            return target;
        }
    }
    return 1U;
}

// Turns the LOD chain into clusters and lays the geometry out in cluster order, so that a
// cluster is a contiguous run of both buffers and a chunk of clusters is a contiguous slice of
// them. That is the property the streaming fragment rests on.
[[nodiscard]] MeshBakeStatus BuildClusters(
    const SourceGeometry& source,
    const std::vector<LodLevel>& lodChain,
    std::uint32_t targetGroupSize,
    ClusteredMesh& out) {
    const std::uint32_t vertexFloats = source.vertexStride / sizeof(float);
    for (std::uint32_t lodIndex = 0U; lodIndex < lodChain.size(); ++lodIndex) {
        const LodLevel& lod = lodChain[lodIndex];
        RenderMeshLodDesc lodDesc{};
        lodDesc.firstSection = static_cast<std::uint32_t>(out.sections.size());
        lodDesc.firstMeshlet = static_cast<std::uint32_t>(out.meshlets.size());
        // Left at zero deliberately. A screen coverage is an error divided by a viewport and a
        // field of view, and baking one is exactly what decision 3 forbids; the absolute error
        // this level carries is recorded in the payload instead, for a runtime that has a
        // camera to divide by.
        lodDesc.minScreenCoverage = 0.0F;

        for (const LodSection& section : lod.sections) {
            const std::uint32_t sectionIndex = static_cast<std::uint32_t>(out.sections.size());
            const std::size_t bound =
                meshopt_buildMeshletsBound(section.indices.size(), kMaxClusterVertices, kMinClusterTriangles);
            std::vector<meshopt_Meshlet> meshlets(bound);
            std::vector<std::uint32_t> meshletVertices(section.indices.size());
            std::vector<std::uint8_t> meshletTriangles(section.indices.size());
            const std::size_t meshletCount = meshopt_buildMeshletsFlex(
                meshlets.data(),
                meshletVertices.data(),
                meshletTriangles.data(),
                section.indices.data(),
                section.indices.size(),
                source.positions,
                source.vertexCount,
                source.vertexStride,
                kMaxClusterVertices,
                kMinClusterTriangles,
                kMaxClusterTriangles,
                kClusterConeWeight,
                kClusterSplitFactor);
            if (meshletCount == 0U) {
                return MeshBakeStatus::ClusterBuildFailed;
            }

            std::vector<std::uint32_t> partitionIndices;
            std::vector<std::uint32_t> partitionIndexCounts(meshletCount, 0U);
            partitionIndices.reserve(section.indices.size());
            for (std::size_t meshletIndex = 0U; meshletIndex < meshletCount; ++meshletIndex) {
                const meshopt_Meshlet& meshlet = meshlets[meshletIndex];
                partitionIndexCounts[meshletIndex] = meshlet.triangle_count * 3U;
                for (std::uint32_t corner = 0U; corner < meshlet.triangle_count * 3U; ++corner) {
                    const std::uint32_t local = meshletTriangles[meshlet.triangle_offset + corner];
                    partitionIndices.push_back(meshletVertices[meshlet.vertex_offset + local]);
                }
            }
            std::vector<std::uint32_t> partitionIds(meshletCount, 0U);
            const std::size_t partitionCount = meshopt_partitionClusters(
                partitionIds.data(),
                partitionIndices.data(),
                partitionIndices.size(),
                partitionIndexCounts.data(),
                meshletCount,
                source.positions,
                source.vertexCount,
                source.vertexStride,
                targetGroupSize);
            if (partitionCount == 0U) {
                return MeshBakeStatus::ClusterBuildFailed;
            }
            std::vector<std::uint32_t> meshletOrder(meshletCount, 0U);
            std::iota(meshletOrder.begin(), meshletOrder.end(), 0U);
            std::ranges::stable_sort(meshletOrder, [&partitionIds](std::uint32_t lhs, std::uint32_t rhs) noexcept {
                return partitionIds[lhs] < partitionIds[rhs];
            });

            const std::uint32_t sectionIndexStart = static_cast<std::uint32_t>(out.indices.size());
            const std::uint32_t sectionVertexStart =
                static_cast<std::uint32_t>(out.vertices.size() / vertexFloats);
            std::vector<std::uint32_t> clusterIndices;
            std::uint32_t previousPartition = std::numeric_limits<std::uint32_t>::max();
            for (const std::uint32_t sourceMeshletIndex : meshletOrder) {
                const std::uint32_t partition = partitionIds[sourceMeshletIndex];
                if (partition != previousPartition) {
                    out.groups.push_back(ClusterGroup{
                        .firstMeshlet = static_cast<std::uint32_t>(out.meshlets.size()),
                        .meshletCount = 0U,
                    });
                    previousPartition = partition;
                }
                const meshopt_Meshlet& meshlet = meshlets[sourceMeshletIndex];
                const std::uint32_t vertexStart =
                    static_cast<std::uint32_t>(out.vertices.size() / vertexFloats);
                const std::uint32_t indexStart = static_cast<std::uint32_t>(out.indices.size());

                clusterIndices.assign(meshlet.triangle_count * 3U, 0U);
                for (std::uint32_t corner = 0U; corner < meshlet.triangle_count * 3U; ++corner) {
                    const std::uint32_t local = meshletTriangles[meshlet.triangle_offset + corner];
                    clusterIndices[corner] = meshletVertices[meshlet.vertex_offset + local];
                    out.indices.push_back(vertexStart + local);
                }
                // The cluster's own bounds and cone, from the triangles that are in it. This is
                // the whole point of the stage: the field these fill has been a constant
                // {0,0,1,1} that no culler could ever have used.
                const meshopt_Bounds bounds = meshopt_computeClusterBounds(
                    clusterIndices.data(),
                    clusterIndices.size(),
                    source.positions,
                    source.vertexCount,
                    source.vertexStride);

                for (std::uint32_t local = 0U; local < meshlet.vertex_count; ++local) {
                    const std::uint32_t sourceVertex = meshletVertices[meshlet.vertex_offset + local];
                    const float* const from = source.positions + static_cast<std::size_t>(sourceVertex) * vertexFloats;
                    out.vertices.insert(out.vertices.end(), from, from + vertexFloats);
                }

                RenderMeshletDesc desc{};
                desc.indexStart = indexStart;
                desc.indexCount = meshlet.triangle_count * 3U;
                desc.vertexStart = vertexStart;
                desc.vertexCount = meshlet.vertex_count;
                desc.sectionIndex = sectionIndex;
                desc.bounds = RenderBoundsSphere{
                    .center = { bounds.center[0], bounds.center[1], bounds.center[2] },
                    .radius = bounds.radius,
                };
                desc.cone = { bounds.cone_axis[0], bounds.cone_axis[1], bounds.cone_axis[2], bounds.cone_cutoff };
                desc.lodLevel = static_cast<std::uint8_t>(lodIndex);
                // A cluster with no extent is one whose triangles are all a single point. Its
                // sphere culls nothing and its cone means nothing, and a reader that trusted
                // RenderMeshletDesc::IsValid would refuse the artifact anyway -- so the bake is
                // the refusal, rather than a payload that only fails on the way back in.
                if (!std::isfinite(desc.bounds.radius) || !desc.bounds.IsValid()) {
                    return MeshBakeStatus::DegenerateGeometry;
                }
                out.meshlets.push_back(desc);
                ++out.groups.back().meshletCount;
            }

            const std::uint32_t sectionVertexCount =
                static_cast<std::uint32_t>(out.vertices.size() / vertexFloats) - sectionVertexStart;
            RenderMeshSectionDesc sectionDesc{};
            sectionDesc.indexStart = sectionIndexStart;
            sectionDesc.indexCount = static_cast<std::uint32_t>(out.indices.size()) - sectionIndexStart;
            sectionDesc.materialSlot = section.materialSlot;
            sectionDesc.bounds = BoundsOfVertices(
                reinterpret_cast<const std::uint8_t*>(out.vertices.data()),
                source.vertexStride,
                sectionVertexStart,
                sectionVertexCount);
            if (!IsFiniteBounds(sectionDesc.bounds)) {
                return MeshBakeStatus::NonFiniteGeometry;
            }
            sectionDesc.lodLevel = static_cast<std::uint8_t>(lodIndex);
            out.sections.push_back(sectionDesc);
        }

        lodDesc.sectionCount = static_cast<std::uint32_t>(out.sections.size()) - lodDesc.firstSection;
        lodDesc.meshletCount = static_cast<std::uint32_t>(out.meshlets.size()) - lodDesc.firstMeshlet;
        out.lods.push_back(lodDesc);
        out.lodErrors.push_back(lod.absoluteError);
    }
    return MeshBakeStatus::Success;
}

// Converts authored material/LOD sections into draw-addressable vertex ranges. A range is
// assembled from whole topology-aware cluster groups, so it can be rebound as one local-index
// draw without cutting a streaming fragment. This is the same split-before-overflow rule used
// by bgfx geometryc, with all ranges kept in one physical vertex and index buffer.
[[nodiscard]] MeshBakeStatus BuildDrawSections(
    ClusteredMesh& mesh,
    std::uint32_t vertexStride,
    std::uint64_t maxSectionVertices) {
    if (mesh.sections.empty() || mesh.meshlets.empty() || mesh.groups.empty() || maxSectionVertices == 0U) {
        return MeshBakeStatus::ClusterBuildFailed;
    }

    const std::vector<RenderMeshSectionDesc> sourceSections = mesh.sections;
    const std::vector<RenderMeshLodDesc> sourceLods = mesh.lods;
    std::vector<RenderMeshSectionDesc> drawSections;
    drawSections.reserve(sourceSections.size());
    std::uint32_t groupCursor = 0U;
    std::uint32_t expectedFirstMeshlet = 0U;

    for (std::size_t lodIndex = 0U; lodIndex < sourceLods.size(); ++lodIndex) {
        const RenderMeshLodDesc& sourceLod = sourceLods[lodIndex];
        RenderMeshLodDesc& drawLod = mesh.lods[lodIndex];
        drawLod.firstSection = static_cast<std::uint32_t>(drawSections.size());

        for (std::uint32_t sectionOffset = 0U; sectionOffset < sourceLod.sectionCount; ++sectionOffset) {
            const std::uint32_t sourceSectionIndex = sourceLod.firstSection + sectionOffset;
            const RenderMeshSectionDesc& sourceSection = sourceSections[sourceSectionIndex];
            bool emittedSection = false;

            while (groupCursor < mesh.groups.size()) {
                const ClusterGroup& firstGroup = mesh.groups[groupCursor];
                if (firstGroup.firstMeshlet >= mesh.meshlets.size() || firstGroup.meshletCount == 0U ||
                    firstGroup.meshletCount > mesh.meshlets.size() - firstGroup.firstMeshlet ||
                    firstGroup.firstMeshlet != expectedFirstMeshlet) {
                    return MeshBakeStatus::ClusterBuildFailed;
                }
                if (mesh.meshlets[firstGroup.firstMeshlet].sectionIndex != sourceSectionIndex) {
                    break;
                }

                const std::uint32_t firstGroupIndex = groupCursor;
                const std::uint32_t sectionFirstMeshlet = firstGroup.firstMeshlet;
                const std::uint32_t sectionVertexStart = mesh.meshlets[sectionFirstMeshlet].vertexStart;
                const std::uint32_t sectionIndexStart = mesh.meshlets[sectionFirstMeshlet].indexStart;
                std::uint64_t sectionVertexCount = 0U;
                std::uint64_t sectionIndexCount = 0U;

                while (groupCursor < mesh.groups.size()) {
                    const ClusterGroup& group = mesh.groups[groupCursor];
                    if (group.firstMeshlet >= mesh.meshlets.size() || group.meshletCount == 0U ||
                        group.meshletCount > mesh.meshlets.size() - group.firstMeshlet ||
                        group.firstMeshlet != expectedFirstMeshlet ||
                        mesh.meshlets[group.firstMeshlet].sectionIndex != sourceSectionIndex) {
                        break;
                    }

                    std::uint64_t groupVertexCount = 0U;
                    std::uint64_t groupIndexCount = 0U;
                    for (std::uint32_t meshletOffset = 0U; meshletOffset < group.meshletCount; ++meshletOffset) {
                        const RenderMeshletDesc& meshlet = mesh.meshlets[group.firstMeshlet + meshletOffset];
                        if (meshlet.sectionIndex != sourceSectionIndex) {
                            return MeshBakeStatus::ClusterBuildFailed;
                        }
                        groupVertexCount += meshlet.vertexCount;
                        groupIndexCount += meshlet.indexCount;
                    }
                    if (groupVertexCount > maxSectionVertices) {
                        return MeshBakeStatus::IndexWidthExceeded;
                    }
                    if (sectionVertexCount != 0U &&
                        sectionVertexCount + groupVertexCount > maxSectionVertices) {
                        break;
                    }
                    sectionVertexCount += groupVertexCount;
                    sectionIndexCount += groupIndexCount;
                    expectedFirstMeshlet += group.meshletCount;
                    ++groupCursor;
                }

                if (sectionVertexCount == 0U || sectionVertexCount > std::numeric_limits<std::uint32_t>::max() ||
                    sectionIndexCount == 0U || sectionIndexCount > std::numeric_limits<std::uint32_t>::max()) {
                    return MeshBakeStatus::ClusterBuildFailed;
                }
                const std::uint32_t drawSectionIndex = static_cast<std::uint32_t>(drawSections.size());
                for (std::uint32_t usedGroup = firstGroupIndex; usedGroup < groupCursor; ++usedGroup) {
                    const ClusterGroup& group = mesh.groups[usedGroup];
                    for (std::uint32_t meshletOffset = 0U; meshletOffset < group.meshletCount; ++meshletOffset) {
                        mesh.meshlets[group.firstMeshlet + meshletOffset].sectionIndex = drawSectionIndex;
                    }
                }

                RenderMeshSectionDesc section = sourceSection;
                section.indexStart = sectionIndexStart;
                section.indexCount = static_cast<std::uint32_t>(sectionIndexCount);
                section.vertexStart = sectionVertexStart;
                section.vertexCount = static_cast<std::uint32_t>(sectionVertexCount);
                section.bounds = BoundsOfVertices(
                    reinterpret_cast<const std::uint8_t*>(mesh.vertices.data()),
                    vertexStride,
                    section.vertexStart,
                    section.vertexCount);
                if (!IsFiniteBounds(section.bounds)) {
                    return MeshBakeStatus::NonFiniteGeometry;
                }
                drawSections.push_back(section);
                emittedSection = true;
            }

            if (!emittedSection) {
                return MeshBakeStatus::ClusterBuildFailed;
            }
        }
        drawLod.sectionCount = static_cast<std::uint32_t>(drawSections.size()) - drawLod.firstSection;
    }

    if (groupCursor != mesh.groups.size() || expectedFirstMeshlet != mesh.meshlets.size()) {
        return MeshBakeStatus::ClusterBuildFailed;
    }
    mesh.sections = std::move(drawSections);
    return MeshBakeStatus::Success;
}

// One topology-aware cluster group is one chunk and therefore one streaming fragment. The
// group boundary belongs to the baker and can never be cut later by a byte-oriented container.
[[nodiscard]] MeshBakeStatus BuildChunks(
    const ClusteredMesh& mesh,
    std::uint32_t vertexStride,
    std::uint32_t indexWidthBytes,
    std::uint64_t maxChunkBytes,
    std::vector<GeometryChunk>& out) {
    // A chunk codec stores indices relative to the chunk and therefore reaches 65536 vertices.
    // Draw sections independently rebase those chunks, so this is a fragment limit rather than
    // a limit on the complete baked asset.
    const std::uint64_t maxChunkVertices =
        indexWidthBytes == 2U ? 65536ULL : static_cast<std::uint64_t>(std::numeric_limits<std::uint32_t>::max());

    for (const ClusterGroup& group : mesh.groups) {
        GeometryChunk chunk{};
        chunk.firstMeshlet = group.firstMeshlet;
        chunk.meshletCount = group.meshletCount;
        const RenderMeshletDesc& first = mesh.meshlets[group.firstMeshlet];
        chunk.vertexStart = first.vertexStart;
        chunk.indexStart = first.indexStart;
        for (std::uint32_t offset = 0U; offset < group.meshletCount; ++offset) {
            const RenderMeshletDesc& meshlet = mesh.meshlets[group.firstMeshlet + offset];
            chunk.vertexCount += meshlet.vertexCount;
            chunk.indexCount += meshlet.indexCount;
        }
        if (chunk.vertexCount > maxChunkVertices ||
            EncodedChunkByteBound(
                chunk.meshletCount, chunk.vertexCount, chunk.indexCount, vertexStride) > maxChunkBytes) {
            return MeshBakeStatus::ChunkBudgetTooSmall;
        }
        out.push_back(chunk);
    }
    return out.empty() ? MeshBakeStatus::EmptySource : MeshBakeStatus::Success;
}

[[nodiscard]] bool EncodeChunk(
    ClusteredMesh& mesh,
    const GeometryChunk& chunk,
    std::uint32_t vertexStride,
    std::uint32_t indexWidthBytes,
    std::vector<std::uint8_t>& bytes) {
    const std::uint32_t vertexFloats = vertexStride / sizeof(float);
    const std::uint8_t* const vertexBase =
        reinterpret_cast<const std::uint8_t*>(
            mesh.vertices.data() + static_cast<std::size_t>(chunk.vertexStart) * vertexFloats);
    std::vector<std::uint8_t> encodedVertices(meshopt_encodeVertexBufferBound(chunk.vertexCount, vertexStride), 0U);
    const std::size_t encodedVertexBytes = meshopt_encodeVertexBuffer(
        encodedVertices.data(), encodedVertices.size(), vertexBase, chunk.vertexCount, vertexStride);
    if (encodedVertexBytes == 0U) {
        return false;
    }
    encodedVertices.resize(encodedVertexBytes);

    std::vector<std::uint32_t> relativeIndices(chunk.indexCount, 0U);
    for (std::uint32_t offset = 0U; offset < chunk.indexCount; ++offset) {
        relativeIndices[offset] = mesh.indices[chunk.indexStart + offset] - chunk.vertexStart;
    }
    std::vector<std::uint8_t> encodedIndices(
        meshopt_encodeIndexBufferBound(chunk.indexCount, chunk.vertexCount), 0U);
    const std::size_t encodedIndexBytes = meshopt_encodeIndexBuffer(
        encodedIndices.data(), encodedIndices.size(), relativeIndices.data(), relativeIndices.size());
    if (encodedIndexBytes == 0U) {
        return false;
    }
    encodedIndices.resize(encodedIndexBytes);

    // The triangle codec is geometry-lossless but may cyclically rotate a triangle while
    // preserving its winding. Floating-point normal construction is not bit-invariant under
    // that rotation, so the culling metadata must be derived from the exact topology the
    // runtime decodes rather than from the pre-codec ordering.
    std::vector<std::uint32_t> decodedIndices(chunk.indexCount, 0U);
    if (meshopt_decodeIndexBuffer(
            decodedIndices.data(),
            decodedIndices.size(),
            sizeof(std::uint32_t),
            encodedIndices.data(),
            encodedIndices.size()) != 0) {
        return false;
    }
    for (std::uint32_t offset = 0U; offset < chunk.indexCount; ++offset) {
        mesh.indices[chunk.indexStart + offset] = chunk.vertexStart + decodedIndices[offset];
    }
    const float* const positions = mesh.vertices.data();
    const std::uint32_t vertexCount = static_cast<std::uint32_t>(mesh.vertices.size() / vertexFloats);
    for (std::uint32_t offset = 0U; offset < chunk.meshletCount; ++offset) {
        RenderMeshletDesc& meshlet = mesh.meshlets[chunk.firstMeshlet + offset];
        const meshopt_Bounds bounds = meshopt_computeClusterBounds(
            mesh.indices.data() + meshlet.indexStart,
            meshlet.indexCount,
            positions,
            vertexCount,
            vertexStride);
        meshlet.bounds = RenderBoundsSphere{
            .center = { bounds.center[0], bounds.center[1], bounds.center[2] },
            .radius = bounds.radius,
        };
        meshlet.cone = { bounds.cone_axis[0], bounds.cone_axis[1], bounds.cone_axis[2], bounds.cone_cutoff };
        if (!IsFiniteBounds(meshlet.bounds) || !IsFiniteCone(meshlet.cone)) {
            return false;
        }
    }

    bytes.clear();
    const std::uint32_t vertexDataOffset =
        static_cast<std::uint32_t>(kChunkHeaderBytes + chunk.meshletCount * kChunkClusterEntryBytes);
    if (encodedVertexBytes > std::numeric_limits<std::uint32_t>::max() - vertexDataOffset) {
        return false;
    }
    const std::uint32_t indexDataOffset = vertexDataOffset + static_cast<std::uint32_t>(encodedVertexBytes);
    bytes.reserve(static_cast<std::size_t>(indexDataOffset) + encodedIndexBytes);

    PutUInt32(bytes, chunk.meshletCount);
    PutUInt32(bytes, chunk.vertexCount);
    PutUInt32(bytes, chunk.indexCount);
    PutUInt32(bytes, indexWidthBytes);
    PutUInt32(bytes, vertexDataOffset);
    PutUInt32(bytes, indexDataOffset);
    for (std::uint32_t offset = 0U; offset < chunk.meshletCount; ++offset) {
        const RenderMeshletDesc& meshlet = mesh.meshlets[chunk.firstMeshlet + offset];
        PutUInt32(bytes, meshlet.vertexStart - chunk.vertexStart);
        PutUInt32(bytes, meshlet.vertexCount);
        PutUInt32(bytes, meshlet.indexStart - chunk.indexStart);
        PutUInt32(bytes, meshlet.indexCount);
    }
    bytes.insert(bytes.end(), encodedVertices.begin(), encodedVertices.end());
    bytes.insert(bytes.end(), encodedIndices.begin(), encodedIndices.end());
    return true;
}

[[nodiscard]] std::vector<std::uint8_t> EncodePrimaryBlock(
    const RenderMeshAssetData& source,
    std::span<const std::uint8_t> materialMetadata,
    const SourceGeometry& geometry,
    const ClusteredMesh& mesh,
    const std::vector<GeometryChunk>& chunks,
    std::span<const std::vector<std::uint8_t>> chunkBytes,
    std::uint32_t indexWidthBytes,
    const RenderBoundsSphere& bounds) {
    std::vector<std::uint8_t> bytes;
    bytes.insert(bytes.end(), kBakedMeshMagic.begin(), kBakedMeshMagic.end());
    PutUInt32(bytes, kBakedMeshFormatVersion);
    PutUInt32(bytes, static_cast<std::uint32_t>(geometry.format));
    PutUInt32(bytes, geometry.vertexStride);
    PutUInt32(bytes, indexWidthBytes);
    PutUInt32(bytes, static_cast<std::uint32_t>(
        mesh.vertices.size() / (geometry.vertexStride / sizeof(float))));
    PutUInt32(bytes, static_cast<std::uint32_t>(mesh.indices.size()));
    PutUInt32(bytes, static_cast<std::uint32_t>(mesh.lods.size()));
    PutUInt32(bytes, static_cast<std::uint32_t>(mesh.sections.size()));
    PutUInt32(bytes, static_cast<std::uint32_t>(mesh.meshlets.size()));
    PutUInt32(bytes, static_cast<std::uint32_t>(chunks.size()));
    PutFloat(bytes, bounds.center[0]);
    PutFloat(bytes, bounds.center[1]);
    PutFloat(bytes, bounds.center[2]);
    PutFloat(bytes, bounds.radius);
    PutUInt32(bytes, static_cast<std::uint32_t>(source.materialSlots.size()));
    PutUInt32(bytes, static_cast<std::uint32_t>(materialMetadata.size()));

    for (const RenderMaterialSlotDesc& slot : source.materialSlots) {
        PutUInt64(bytes, slot.defaultMaterialAssetId);
    }
    bytes.insert(bytes.end(), materialMetadata.begin(), materialMetadata.end());
    for (std::size_t lodIndex = 0U; lodIndex < mesh.lods.size(); ++lodIndex) {
        const RenderMeshLodDesc& lod = mesh.lods[lodIndex];
        std::uint32_t lodIndexCount = 0U;
        for (std::uint32_t offset = 0U; offset < lod.sectionCount; ++offset) {
            lodIndexCount += mesh.sections[lod.firstSection + offset].indexCount;
        }
        PutUInt32(bytes, lod.firstSection);
        PutUInt32(bytes, lod.sectionCount);
        PutUInt32(bytes, lod.firstMeshlet);
        PutUInt32(bytes, lod.meshletCount);
        PutUInt32(bytes, lodIndexCount);
        PutFloat(bytes, mesh.lodErrors[lodIndex]);
        PutFloat(bytes, lod.minScreenCoverage);
    }
    for (const RenderMeshSectionDesc& section : mesh.sections) {
        PutUInt32(bytes, section.indexStart);
        PutUInt32(bytes, section.indexCount);
        PutUInt32(bytes, section.materialSlot);
        PutUInt32(bytes, section.lodLevel);
        PutFloat(bytes, section.bounds.center[0]);
        PutFloat(bytes, section.bounds.center[1]);
        PutFloat(bytes, section.bounds.center[2]);
        PutFloat(bytes, section.bounds.radius);
        PutUInt32(bytes, section.vertexStart);
        PutUInt32(bytes, section.vertexCount);
    }
    for (const RenderMeshletDesc& meshlet : mesh.meshlets) {
        PutUInt32(bytes, meshlet.indexStart);
        PutUInt32(bytes, meshlet.indexCount);
        PutUInt32(bytes, meshlet.vertexStart);
        PutUInt32(bytes, meshlet.vertexCount);
        PutUInt32(bytes, meshlet.sectionIndex);
        PutUInt32(bytes, meshlet.lodLevel);
        PutFloat(bytes, meshlet.bounds.center[0]);
        PutFloat(bytes, meshlet.bounds.center[1]);
        PutFloat(bytes, meshlet.bounds.center[2]);
        PutFloat(bytes, meshlet.bounds.radius);
        PutFloat(bytes, meshlet.cone[0]);
        PutFloat(bytes, meshlet.cone[1]);
        PutFloat(bytes, meshlet.cone[2]);
        PutFloat(bytes, meshlet.cone[3]);
    }
    for (std::size_t chunkIndex = 0U; chunkIndex < chunks.size(); ++chunkIndex) {
        const GeometryChunk& chunk = chunks[chunkIndex];
        PutUInt32(bytes, chunk.firstMeshlet);
        PutUInt32(bytes, chunk.meshletCount);
        PutUInt32(bytes, chunk.vertexStart);
        PutUInt32(bytes, chunk.vertexCount);
        PutUInt32(bytes, chunk.indexStart);
        PutUInt32(bytes, chunk.indexCount);
        PutUInt32(bytes, static_cast<std::uint32_t>(chunkBytes[chunkIndex].size()));
    }
    return bytes;
}

// The bytes the key hashes as "the source". Explicit, length prefixed and little-endian by
// construction, so two machines that were handed the same mesh compute the same key.
[[nodiscard]] std::vector<std::uint8_t> SerializeSourceGeometry(const RenderMeshAssetData& source) {
    const bool tangents = SourceUsesTangents(source);
    const std::uint32_t vertexStride = static_cast<std::uint32_t>(
        tangents ? sizeof(RenderStaticMeshVertexP3N3T4UV2) : sizeof(RenderStaticMeshVertexP3N3UV2));
    const std::size_t vertexCount = tangents ? source.tangentVertices.size() : source.vertices.size();
    const std::uint8_t* const vertexBytes = tangents
        ? reinterpret_cast<const std::uint8_t*>(source.tangentVertices.data())
        : reinterpret_cast<const std::uint8_t*>(source.vertices.data());

    std::vector<std::uint8_t> bytes;
    PutUInt32(bytes, tangents ? 1U : 0U);
    PutUInt32(bytes, vertexStride);
    PutUInt64(bytes, vertexCount);
    // Every vertex is a run of floats, so the bytes are the values; there is no padding in
    // either layout to make this machine dependent.
    if (vertexCount != 0U) {
        bytes.insert(bytes.end(), vertexBytes, vertexBytes + vertexCount * vertexStride);
    }
    if (!source.indices32.empty()) {
        PutUInt64(bytes, source.indices32.size());
        for (const std::uint32_t index : source.indices32) {
            PutUInt32(bytes, index);
        }
    } else {
        PutUInt64(bytes, source.indices16.size());
        for (const std::uint16_t index : source.indices16) {
            PutUInt32(bytes, index);
        }
    }
    PutUInt64(bytes, source.sections.size());
    for (const RenderMeshSectionDesc& section : source.sections) {
        PutUInt32(bytes, section.indexStart);
        PutUInt32(bytes, section.indexCount);
        PutUInt32(bytes, section.vertexStart);
        PutUInt32(bytes, section.vertexCount);
        PutUInt32(bytes, section.materialSlot);
        PutUInt32(bytes, section.lodLevel);
        PutUInt32(bytes, section.terrainLayerIndex);
    }
    PutUInt64(bytes, source.materialSlots.size());
    for (const RenderMaterialSlotDesc& slot : source.materialSlots) {
        PutUInt64(bytes, slot.defaultMaterialAssetId);
    }
    const std::vector<std::uint8_t> materialMetadata = SerializeMaterialMetadata(source);
    PutUInt64(bytes, materialMetadata.size());
    bytes.insert(bytes.end(), materialMetadata.begin(), materialMetadata.end());
    return bytes;
}

// The parameters of the algorithm itself. They are not in the profile and not in the source, so
// without them a cluster size change would leave every mesh addressable under the key it had --
// the same defect the profile fingerprint closed for the target description.
[[nodiscard]] std::vector<std::uint8_t> SerializeMeshBakeParameters() {
    std::vector<std::uint8_t> bytes;
    PutUInt32(bytes, kMaxClusterVertices);
    PutUInt32(bytes, kMinClusterTriangles);
    PutUInt32(bytes, kMaxClusterTriangles);
    PutFloat(bytes, kClusterConeWeight);
    PutFloat(bytes, kClusterSplitFactor);
    PutUInt32(bytes, kTargetClusterGroupSize);
    PutUInt32(bytes, kMaxLodCount);
    PutFloat(bytes, kLodTriangleRatio);
    PutFloat(bytes, kLodErrorFractionOfScale);
    PutFloat(bytes, kSimplifyNormalWeight);
    PutFloat(bytes, kSimplifyTangentWeight);
    PutFloat(bytes, kSimplifyUvWeight);
    PutFloat(bytes, kSimplifyColorWeight);
    return bytes;
}

} // namespace

std::string_view ToString(MeshBakeStatus status) noexcept {
    switch (status) {
    case MeshBakeStatus::Success:
        return "Success";
    case MeshBakeStatus::InvalidProfile:
        return "InvalidProfile";
    case MeshBakeStatus::EmptySource:
        return "EmptySource";
    case MeshBakeStatus::MalformedIndices:
        return "MalformedIndices";
    case MeshBakeStatus::MalformedSections:
        return "MalformedSections";
    case MeshBakeStatus::MalformedMaterials:
        return "MalformedMaterials";
    case MeshBakeStatus::NonFiniteGeometry:
        return "NonFiniteGeometry";
    case MeshBakeStatus::DegenerateGeometry:
        return "DegenerateGeometry";
    case MeshBakeStatus::UnsupportedSourceShape:
        return "UnsupportedSourceShape";
    case MeshBakeStatus::ClusterBuildFailed:
        return "ClusterBuildFailed";
    case MeshBakeStatus::SimplificationFailed:
        return "SimplificationFailed";
    case MeshBakeStatus::IndexWidthExceeded:
        return "IndexWidthExceeded";
    case MeshBakeStatus::ResidentBudgetExceeded:
        return "ResidentBudgetExceeded";
    case MeshBakeStatus::EncodeFailed:
        return "EncodeFailed";
    case MeshBakeStatus::ChunkBudgetTooSmall:
        return "ChunkBudgetTooSmall";
    case MeshBakeStatus::SinkRejected:
        return "SinkRejected";
    }
    return "Unknown";
}

std::string BakedMeshChunkBlockName(std::uint32_t chunkIndex) {
    return "geom" + std::to_string(chunkIndex);
}

AssetBakeKey MakeMeshBakeKey(const RenderMeshAssetData& source, const BakeTargetProfile& profile) {
    const std::vector<std::uint8_t> serializedSource = SerializeSourceGeometry(source);
    const std::vector<std::uint8_t> serializedParameters = SerializeMeshBakeParameters();
    AssetBakeKey key{};
    key.sourceContentHash = kb::assets::bake::HashBakeBytes(serializedSource);
    key.bakerId = std::string{ kMeshBakerId };
    key.bakerVersion = std::string{ kMeshBakerVersion };
    key.targetProfileId = std::string{ profile.identifier };
    key.targetProfileHash = kb::assets::bake::BakeTargetProfileFingerprint(profile);
    key.settingsHash = kb::assets::bake::HashBakeBytes(serializedParameters);
    return key;
}

MeshBakeOutput BakeMesh(
    const RenderMeshAssetData& source,
    const BakeTargetProfile& profile,
    kb::assets::bake::IBakedAssetSink& sink) {
    MeshBakeOutput output{};

    if (!kb::assets::bake::IsValidBakeTargetProfile(profile)) {
        output.status = MeshBakeStatus::InvalidProfile;
        return output;
    }

    output.key = MakeMeshBakeKey(source, profile);

    SourceGeometry geometry{};
    if (const MeshBakeStatus status = ReadSourceGeometry(source, geometry); status != MeshBakeStatus::Success) {
        output.status = status;
        return output;
    }
    const std::vector<std::uint8_t> materialMetadata = SerializeMaterialMetadata(source);
    if (!IsValidSourceMaterials(source, materialMetadata)) {
        output.status = MeshBakeStatus::MalformedMaterials;
        return output;
    }

    std::vector<LodLevel> lodChain;
    if (const MeshBakeStatus status = BuildLodChain(geometry, lodChain); status != MeshBakeStatus::Success) {
        output.status = status;
        return output;
    }
    ClusteredMesh mesh{};
    const std::uint32_t targetGroupSize = TargetClusterGroupSize(geometry.vertexStride, profile.maxGeometryChunkBytes);
    if (const MeshBakeStatus status = BuildClusters(geometry, lodChain, targetGroupSize, mesh);
        status != MeshBakeStatus::Success) {
        output.status = status;
        return output;
    }

    const std::uint32_t indexWidthBytes = kb::assets::bake::BakeIndexWidthBytes(profile.indexWidth);
    const std::uint64_t maxSectionVertices = indexWidthBytes == 2U
        ? 65536ULL
        : static_cast<std::uint64_t>(std::numeric_limits<std::uint32_t>::max());
    if (const MeshBakeStatus status = BuildDrawSections(mesh, geometry.vertexStride, maxSectionVertices);
        status != MeshBakeStatus::Success) {
        output.status = status;
        return output;
    }
    const std::uint64_t residentGeometryBytes =
        static_cast<std::uint64_t>(mesh.vertices.size()) * sizeof(float) +
        static_cast<std::uint64_t>(mesh.indices.size()) * indexWidthBytes;
    if (residentGeometryBytes > kMaxResidentGeometryBytes) {
        output.status = MeshBakeStatus::ResidentBudgetExceeded;
        return output;
    }
    std::vector<GeometryChunk> chunks;
    if (const MeshBakeStatus status = BuildChunks(
            mesh, geometry.vertexStride, indexWidthBytes, profile.maxGeometryChunkBytes, chunks);
        status != MeshBakeStatus::Success) {
        output.status = status;
        return output;
    }

    std::vector<std::vector<std::uint8_t>> chunkBytes;
    chunkBytes.reserve(chunks.size());
    for (const GeometryChunk& chunk : chunks) {
        std::vector<std::uint8_t> encoded;
        if (!EncodeChunk(mesh, chunk, geometry.vertexStride, indexWidthBytes, encoded)) {
            output.status = MeshBakeStatus::EncodeFailed;
            return output;
        }
        if (encoded.size() > profile.maxGeometryChunkBytes) {
            output.status = MeshBakeStatus::ChunkBudgetTooSmall;
            return output;
        }
        chunkBytes.push_back(std::move(encoded));
    }
    const RenderBoundsSphere bakedBounds = BoundsOfVertices(
        reinterpret_cast<const std::uint8_t*>(mesh.vertices.data()),
        geometry.vertexStride,
        0U,
        static_cast<std::uint32_t>(
            mesh.vertices.size() / (geometry.vertexStride / sizeof(float))));
    if (!IsFiniteBounds(bakedBounds)) {
        output.status = MeshBakeStatus::NonFiniteGeometry;
        return output;
    }
    std::vector<std::uint8_t> primaryBlock =
        EncodePrimaryBlock(
            source, materialMetadata, geometry, mesh, chunks, chunkBytes, indexWidthBytes, bakedBounds);
    if (primaryBlock.size() > kb::assets::bake::kMaxAssetPackBlockBytes) {
        output.status = MeshBakeStatus::EncodeFailed;
        return output;
    }

    BakedAssetDescriptor descriptor{};
    descriptor.key = output.key;
    descriptor.assetTypeId = std::string{ kMeshBakedAssetTypeId };
    output.sinkStatus = sink.BeginAsset(descriptor);
    if (output.sinkStatus != BakedAssetSinkStatus::Success) {
        output.status = MeshBakeStatus::SinkRejected;
        return output;
    }
    output.sinkStatus = sink.WritePrimaryBlock(primaryBlock, profile.packageBlockAlignmentBytes);
    if (output.sinkStatus != BakedAssetSinkStatus::Success) {
        sink.AbortAsset();
        output.status = MeshBakeStatus::SinkRejected;
        return output;
    }
    for (std::uint32_t chunkIndex = 0U; chunkIndex < chunks.size(); ++chunkIndex) {
        const GeometryChunk& chunk = chunks[chunkIndex];
        const VertexAabb box = AabbOfVertices(
            reinterpret_cast<const std::uint8_t*>(mesh.vertices.data()),
            geometry.vertexStride,
            chunk.vertexStart,
            chunk.vertexCount);
        BakedAssetBlockFragment fragment{};
        fragment.clusterCount = chunk.meshletCount;
        fragment.boundsMin = box.low;
        fragment.boundsMax = box.high;
        const std::string blockName = BakedMeshChunkBlockName(chunkIndex);
        BakedAssetBlock block{};
        block.name = blockName;
        // Streaming, because that is what a fragment is for: the primary block is enough to
        // know what the mesh is and where its clusters are, and the geometry follows on demand.
        block.residency = BakedAssetBlockResidency::Streaming;
        block.alignmentBytes = profile.packageBlockAlignmentBytes;
        block.fragment = fragment;
        output.sinkStatus = sink.WriteAuxiliaryBlock(block, chunkBytes[chunkIndex]);
        if (output.sinkStatus != BakedAssetSinkStatus::Success) {
            sink.AbortAsset();
            output.status = MeshBakeStatus::SinkRejected;
            return output;
        }
    }
    output.sinkStatus = sink.CommitAsset();
    if (output.sinkStatus != BakedAssetSinkStatus::Success) {
        sink.AbortAsset();
        output.status = MeshBakeStatus::SinkRejected;
        return output;
    }

    output.status = MeshBakeStatus::Success;
    output.lodCount = static_cast<std::uint32_t>(mesh.lods.size());
    output.sectionCount = static_cast<std::uint32_t>(mesh.sections.size());
    output.clusterCount = static_cast<std::uint32_t>(mesh.meshlets.size());
    output.primaryBlock = std::move(primaryBlock);
    output.chunks = std::move(chunkBytes);
    return output;
}

bool BakedMeshMatchesTargetProfile(
    std::span<const std::uint8_t> primaryBlock,
    std::span<const std::vector<std::uint8_t>> chunks,
    const kb::assets::bake::BakeTargetProfile& profile) noexcept {
    if (!kb::assets::bake::IsValidBakeTargetProfile(profile) ||
        primaryBlock.size() < kPrimaryHeaderBytes ||
        std::memcmp(primaryBlock.data(), kBakedMeshMagic.data(), kBakedMeshMagic.size()) != 0 ||
        PeekUInt32(primaryBlock, 8U) != kBakedMeshFormatVersion ||
        PeekUInt32(primaryBlock, 20U) != kb::assets::bake::BakeIndexWidthBytes(profile.indexWidth) ||
        PeekUInt32(primaryBlock, 44U) != chunks.size()) {
        return false;
    }
    return std::ranges::all_of(chunks, [&profile](const std::vector<std::uint8_t>& chunk) {
        return !chunk.empty() && chunk.size() <= profile.maxGeometryChunkBytes;
    });
}

[[nodiscard]] static bool ReadBakedMeshImpl(
    std::span<const std::uint8_t> primaryBlock,
    std::span<const std::vector<std::uint8_t>> chunks,
    RenderMeshAssetData& out) {
    if (primaryBlock.size() < kPrimaryHeaderBytes ||
        primaryBlock.size() > kb::assets::bake::kMaxAssetPackBlockBytes) {
        return false;
    }
    if (std::memcmp(primaryBlock.data(), kBakedMeshMagic.data(), kBakedMeshMagic.size()) != 0) {
        return false;
    }
    if (PeekUInt32(primaryBlock, 8U) != kBakedMeshFormatVersion) {
        return false;
    }
    const std::uint32_t format = PeekUInt32(primaryBlock, 12U);
    const std::uint32_t vertexStride = PeekUInt32(primaryBlock, 16U);
    const std::uint32_t indexWidthBytes = PeekUInt32(primaryBlock, 20U);
    const std::uint32_t vertexCount = PeekUInt32(primaryBlock, 24U);
    const std::uint32_t indexCount = PeekUInt32(primaryBlock, 28U);
    const std::uint32_t lodCount = PeekUInt32(primaryBlock, 32U);
    const std::uint32_t sectionCount = PeekUInt32(primaryBlock, 36U);
    const std::uint32_t meshletCount = PeekUInt32(primaryBlock, 40U);
    const std::uint32_t chunkCount = PeekUInt32(primaryBlock, 44U);
    const std::uint32_t materialSlotCount = PeekUInt32(primaryBlock, 64U);
    const std::uint32_t materialMetadataBytes = PeekUInt32(primaryBlock, 68U);

    const bool tangents = format == static_cast<std::uint32_t>(BakedMeshVertexFormat::PositionNormalTangentUv);
    if (format > static_cast<std::uint32_t>(BakedMeshVertexFormat::PositionNormalTangentUv)) {
        return false;
    }
    const std::uint32_t expectedStride = static_cast<std::uint32_t>(
        tangents ? sizeof(RenderStaticMeshVertexP3N3T4UV2) : sizeof(RenderStaticMeshVertexP3N3UV2));
    if (vertexStride != expectedStride || (indexWidthBytes != 2U && indexWidthBytes != 4U)) {
        return false;
    }
    if (vertexCount == 0U || indexCount == 0U || indexCount % 3U != 0U || lodCount == 0U ||
        lodCount > kMaxLodCount || sectionCount == 0U || materialSlotCount > kMaxMaterialEntries ||
        materialMetadataBytes > kMaxMaterialMetadataBytes ||
        meshletCount == 0U || chunkCount == 0U || chunks.size() != chunkCount) {
        return false;
    }

    // Counts come from disk. Keep the arithmetic wider than size_t so wasm32 cannot wrap a
    // gigantic table into the length of a tiny primary block and reserve from the raw count.
    const std::uint64_t tablesBytes = static_cast<std::uint64_t>(materialSlotCount) * kMaterialSlotEntryBytes +
        materialMetadataBytes +
        static_cast<std::uint64_t>(lodCount) * kLodEntryBytes +
        static_cast<std::uint64_t>(sectionCount) * kSectionEntryBytes +
        static_cast<std::uint64_t>(meshletCount) * kMeshletEntryBytes +
        static_cast<std::uint64_t>(chunkCount) * kChunkEntryBytes;
    if (tablesBytes > std::numeric_limits<std::size_t>::max() ||
        static_cast<std::uint64_t>(primaryBlock.size()) != kPrimaryHeaderBytes + tablesBytes) {
        return false;
    }
    const std::uint64_t vertexStorageBytes = static_cast<std::uint64_t>(vertexCount) * vertexStride;
    const std::uint64_t indexStorageBytes = static_cast<std::uint64_t>(indexCount) * indexWidthBytes;
    if (vertexStorageBytes > std::numeric_limits<std::size_t>::max() ||
        indexStorageBytes > std::numeric_limits<std::size_t>::max() ||
        vertexStorageBytes + indexStorageBytes > kMaxResidentGeometryBytes) {
        return false;
    }

    std::size_t cursor = kPrimaryHeaderBytes;
    std::vector<RenderMaterialSlotDesc> materialSlots;
    materialSlots.reserve(materialSlotCount);
    for (std::uint32_t slot = 0U; slot < materialSlotCount; ++slot) {
        materialSlots.push_back(RenderMaterialSlotDesc{ .defaultMaterialAssetId = PeekUInt64(primaryBlock, cursor) });
        cursor += kMaterialSlotEntryBytes;
    }
    std::vector<std::string> materialNames;
    std::vector<RenderMeshEmbeddedMaterial> embeddedMaterials;
    if (!ReadMaterialMetadata(
            primaryBlock.subspan(cursor, materialMetadataBytes),
            materialSlotCount,
            materialNames,
            embeddedMaterials)) {
        return false;
    }
    cursor += materialMetadataBytes;

    std::vector<RenderMeshLodDesc> lods;
    std::vector<float> lodErrors;
    std::vector<std::uint32_t> lodIndexCounts;
    lods.reserve(lodCount);
    lodErrors.reserve(lodCount);
    lodIndexCounts.reserve(lodCount);
    std::uint32_t expectedFirstSection = 0U;
    std::uint32_t expectedFirstMeshlet = 0U;
    float previousError = -1.0F;
    for (std::uint32_t lodIndex = 0U; lodIndex < lodCount; ++lodIndex) {
        RenderMeshLodDesc lod{};
        lod.firstSection = PeekUInt32(primaryBlock, cursor + 0U);
        lod.sectionCount = PeekUInt32(primaryBlock, cursor + 4U);
        lod.firstMeshlet = PeekUInt32(primaryBlock, cursor + 8U);
        lod.meshletCount = PeekUInt32(primaryBlock, cursor + 12U);
        const std::uint32_t lodIndexCount = PeekUInt32(primaryBlock, cursor + 16U);
        const float error = PeekFloat(primaryBlock, cursor + 20U);
        lod.minScreenCoverage = PeekFloat(primaryBlock, cursor + 24U);
        cursor += kLodEntryBytes;
        // The levels partition the section and cluster tables exactly, in order; a table that
        // does not is one whose ranges could overlap or leave clusters no level owns.
        if (lod.firstSection != expectedFirstSection || lod.firstMeshlet != expectedFirstMeshlet ||
            lod.sectionCount == 0U || lod.meshletCount == 0U || lodIndexCount == 0U) {
            return false;
        }
        if (lod.sectionCount > sectionCount - lod.firstSection || lod.meshletCount > meshletCount - lod.firstMeshlet) {
            return false;
        }
        // Error is recorded, absolute and never decreasing down the chain; a level that claims
        // less error than the level above it is not an approximation of it.
        if (!std::isfinite(error) || error < 0.0F || error < previousError ||
            (lodIndex == 0U && error != 0.0F) || lod.minScreenCoverage != 0.0F) {
            return false;
        }
        previousError = error;
        expectedFirstSection += lod.sectionCount;
        expectedFirstMeshlet += lod.meshletCount;
        lods.push_back(lod);
        lodErrors.push_back(error);
        lodIndexCounts.push_back(lodIndexCount);
    }
    if (expectedFirstSection != sectionCount || expectedFirstMeshlet != meshletCount) {
        return false;
    }

    std::vector<RenderMeshSectionDesc> sections;
    sections.reserve(sectionCount);
    std::vector<std::uint8_t> sectionLodLevels(sectionCount, 0U);
    for (std::uint32_t lodIndex = 0U; lodIndex < lodCount; ++lodIndex) {
        const RenderMeshLodDesc& lod = lods[lodIndex];
        std::fill_n(
            sectionLodLevels.begin() + static_cast<std::ptrdiff_t>(lod.firstSection),
            lod.sectionCount,
            static_cast<std::uint8_t>(lodIndex));
    }
    std::vector<std::uint64_t> decodedLodIndexCounts(lodCount, 0U);
    for (std::uint32_t sectionIndex = 0U; sectionIndex < sectionCount; ++sectionIndex) {
        RenderMeshSectionDesc section{};
        section.indexStart = PeekUInt32(primaryBlock, cursor + 0U);
        section.indexCount = PeekUInt32(primaryBlock, cursor + 4U);
        section.materialSlot = PeekUInt32(primaryBlock, cursor + 8U);
        const std::uint32_t lodLevel = PeekUInt32(primaryBlock, cursor + 12U);
        section.bounds.center = { PeekFloat(primaryBlock, cursor + 16U),
            PeekFloat(primaryBlock, cursor + 20U),
            PeekFloat(primaryBlock, cursor + 24U) };
        section.bounds.radius = PeekFloat(primaryBlock, cursor + 28U);
        section.vertexStart = PeekUInt32(primaryBlock, cursor + 32U);
        section.vertexCount = PeekUInt32(primaryBlock, cursor + 36U);
        cursor += kSectionEntryBytes;
        if (section.indexCount == 0U || section.indexCount % 3U != 0U || section.indexStart > indexCount ||
            section.indexCount > indexCount - section.indexStart || lodLevel >= lodCount ||
            section.vertexCount == 0U || section.vertexStart > vertexCount ||
            section.vertexCount > vertexCount - section.vertexStart ||
            (indexWidthBytes == 2U && section.vertexCount > 65536U) ||
            lodLevel != sectionLodLevels[sectionIndex] || !IsFiniteBounds(section.bounds) ||
            (materialSlotCount == 0U ? section.materialSlot != 0U : section.materialSlot >= materialSlotCount)) {
            return false;
        }
        section.lodLevel = static_cast<std::uint8_t>(lodLevel);
        decodedLodIndexCounts[lodLevel] += section.indexCount;
        sections.push_back(section);
    }
    for (std::uint32_t lodIndex = 0U; lodIndex < lodCount; ++lodIndex) {
        if (decodedLodIndexCounts[lodIndex] != lodIndexCounts[lodIndex]) {
            return false;
        }
    }

    std::vector<RenderMeshletDesc> meshlets;
    meshlets.reserve(meshletCount);
    // The clusters lie end to end over both buffers and, inside that, section by section. Both
    // are followed here rather than assumed: a cluster table that merely stays in range can
    // still leave triangles no cluster owns and give a section a range its clusters do not fill.
    std::uint64_t expectedMeshletIndex = 0U;
    std::uint64_t expectedMeshletVertex = 0U;
    std::uint32_t previousSectionIndex = 0U;
    std::vector<std::uint32_t> sectionFirstIndex(sectionCount, std::numeric_limits<std::uint32_t>::max());
    std::vector<std::uint32_t> sectionFirstVertex(sectionCount, std::numeric_limits<std::uint32_t>::max());
    std::vector<std::uint64_t> sectionIndexTotal(sectionCount, 0U);
    std::vector<std::uint64_t> sectionVertexTotal(sectionCount, 0U);
    for (std::uint32_t meshletIndex = 0U; meshletIndex < meshletCount; ++meshletIndex) {
        RenderMeshletDesc meshlet{};
        meshlet.indexStart = PeekUInt32(primaryBlock, cursor + 0U);
        meshlet.indexCount = PeekUInt32(primaryBlock, cursor + 4U);
        meshlet.vertexStart = PeekUInt32(primaryBlock, cursor + 8U);
        meshlet.vertexCount = PeekUInt32(primaryBlock, cursor + 12U);
        meshlet.sectionIndex = PeekUInt32(primaryBlock, cursor + 16U);
        const std::uint32_t lodLevel = PeekUInt32(primaryBlock, cursor + 20U);
        meshlet.bounds.center = { PeekFloat(primaryBlock, cursor + 24U),
            PeekFloat(primaryBlock, cursor + 28U),
            PeekFloat(primaryBlock, cursor + 32U) };
        meshlet.bounds.radius = PeekFloat(primaryBlock, cursor + 36U);
        meshlet.cone = { PeekFloat(primaryBlock, cursor + 40U),
            PeekFloat(primaryBlock, cursor + 44U),
            PeekFloat(primaryBlock, cursor + 48U),
            PeekFloat(primaryBlock, cursor + 52U) };
        cursor += kMeshletEntryBytes;
        if (meshlet.indexCount == 0U || meshlet.indexCount % 3U != 0U ||
            meshlet.indexCount > kMaxClusterTriangles * 3U || meshlet.vertexCount == 0U ||
            meshlet.vertexCount > kMaxClusterVertices || meshlet.indexStart > indexCount ||
            meshlet.indexCount > indexCount - meshlet.indexStart || meshlet.vertexStart > vertexCount ||
            meshlet.vertexCount > vertexCount - meshlet.vertexStart || meshlet.sectionIndex >= sectionCount ||
            lodLevel >= lodCount || !IsFiniteBounds(meshlet.bounds) || !IsFiniteCone(meshlet.cone)) {
            return false;
        }
        if (meshlet.indexStart != expectedMeshletIndex || meshlet.vertexStart != expectedMeshletVertex) {
            return false;
        }
        const RenderMeshSectionDesc& section = sections[meshlet.sectionIndex];
        if (meshlet.sectionIndex < previousSectionIndex || lodLevel != section.lodLevel ||
            meshlet.vertexStart < section.vertexStart ||
            meshlet.vertexStart - section.vertexStart > section.vertexCount ||
            meshlet.vertexCount > section.vertexCount - (meshlet.vertexStart - section.vertexStart)) {
            return false;
        }
        previousSectionIndex = meshlet.sectionIndex;
        if (sectionFirstIndex[meshlet.sectionIndex] == std::numeric_limits<std::uint32_t>::max()) {
            sectionFirstIndex[meshlet.sectionIndex] = meshlet.indexStart;
            sectionFirstVertex[meshlet.sectionIndex] = meshlet.vertexStart;
        }
        sectionIndexTotal[meshlet.sectionIndex] += meshlet.indexCount;
        sectionVertexTotal[meshlet.sectionIndex] += meshlet.vertexCount;
        expectedMeshletIndex += meshlet.indexCount;
        expectedMeshletVertex += meshlet.vertexCount;
        meshlet.lodLevel = static_cast<std::uint8_t>(lodLevel);
        meshlets.push_back(meshlet);
    }
    if (expectedMeshletIndex != indexCount || expectedMeshletVertex != vertexCount) {
        return false;
    }
    for (std::uint32_t sectionIndex = 0U; sectionIndex < sectionCount; ++sectionIndex) {
        if (sectionFirstIndex[sectionIndex] != sections[sectionIndex].indexStart ||
            sectionIndexTotal[sectionIndex] != sections[sectionIndex].indexCount ||
            sectionFirstVertex[sectionIndex] != sections[sectionIndex].vertexStart ||
            sectionVertexTotal[sectionIndex] != sections[sectionIndex].vertexCount) {
            return false;
        }
    }

    // Validate every chunk and its real payload before allocating from the aggregate counts in
    // the primary header. A hostile 160-byte primary block must not be able to ask for hundreds
    // of gigabytes merely by setting vertexCount to UINT32_MAX.
    std::vector<GeometryChunk> decodedChunks;
    decodedChunks.reserve(chunkCount);
    std::uint64_t expectedChunkMeshlet = 0U;
    std::uint64_t expectedChunkVertex = 0U;
    std::uint64_t expectedChunkIndex = 0U;
    for (std::uint32_t chunkIndex = 0U; chunkIndex < chunkCount; ++chunkIndex) {
        const std::uint32_t firstMeshlet = PeekUInt32(primaryBlock, cursor + 0U);
        const std::uint32_t chunkMeshlets = PeekUInt32(primaryBlock, cursor + 4U);
        const std::uint32_t chunkVertexStart = PeekUInt32(primaryBlock, cursor + 8U);
        const std::uint32_t chunkVertexCount = PeekUInt32(primaryBlock, cursor + 12U);
        const std::uint32_t chunkIndexStart = PeekUInt32(primaryBlock, cursor + 16U);
        const std::uint32_t chunkIndexCount = PeekUInt32(primaryBlock, cursor + 20U);
        const std::uint32_t byteLength = PeekUInt32(primaryBlock, cursor + 24U);
        cursor += kChunkEntryBytes;
        // The chunks partition the clusters and both buffers exactly, in order. Anything else
        // is geometry that two chunks claim or that no chunk carries.
        if (firstMeshlet != expectedChunkMeshlet || chunkVertexStart != expectedChunkVertex ||
            chunkIndexStart != expectedChunkIndex || chunkMeshlets == 0U || chunkVertexCount == 0U ||
            chunkIndexCount == 0U || chunkMeshlets > meshletCount - firstMeshlet ||
            chunkVertexCount > vertexCount - chunkVertexStart || chunkIndexCount > indexCount - chunkIndexStart) {
            return false;
        }
        if (indexWidthBytes == 2U && chunkVertexCount > 65536U) {
            return false;
        }
        const std::vector<std::uint8_t>& payload = chunks[chunkIndex];
        if (byteLength != payload.size() || payload.size() < kChunkHeaderBytes ||
            payload.size() > kb::assets::bake::kMaxAssetPackBlockBytes) {
            return false;
        }
        const std::uint32_t vertexDataOffset =
            static_cast<std::uint32_t>(kChunkHeaderBytes + chunkMeshlets * kChunkClusterEntryBytes);
        const std::uint32_t indexDataOffset = PeekUInt32(payload, 20U);
        if (PeekUInt32(payload, 0U) != chunkMeshlets || PeekUInt32(payload, 4U) != chunkVertexCount ||
            PeekUInt32(payload, 8U) != chunkIndexCount || PeekUInt32(payload, 12U) != indexWidthBytes ||
            PeekUInt32(payload, 16U) != vertexDataOffset || vertexDataOffset >= indexDataOffset ||
            indexDataOffset >= payload.size()) {
            return false;
        }
        const std::size_t vertexStreamBytes = indexDataOffset - vertexDataOffset;
        const std::size_t indexStreamBytes = payload.size() - indexDataOffset;
        const int vertexCodecVersion = meshopt_decodeVertexVersion(
            payload.data() + vertexDataOffset, vertexStreamBytes);
        const int indexCodecVersion = meshopt_decodeIndexVersion(
            payload.data() + indexDataOffset, indexStreamBytes);
        const std::size_t vertexTailBytes = vertexCodecVersion == 0
            ? std::max<std::size_t>(vertexStride, 32U)
            : std::max<std::size_t>(vertexStride + vertexStride / 4U, 24U);
        const std::uint64_t minimumIndexStreamBytes = 1ULL + chunkIndexCount / 3U + 16ULL;
        if (vertexCodecVersion < 0 || indexCodecVersion < 0 ||
            vertexStreamBytes < 1U + vertexTailBytes || indexStreamBytes < minimumIndexStreamBytes) {
            return false;
        }
        std::uint64_t meshletVertexTotal = 0U;
        std::uint64_t meshletIndexTotal = 0U;
        const std::uint32_t chunkSectionIndex = meshlets[firstMeshlet].sectionIndex;
        const RenderMeshSectionDesc& chunkSection = sections[chunkSectionIndex];
        if (chunkVertexStart < chunkSection.vertexStart ||
            chunkVertexStart - chunkSection.vertexStart > chunkSection.vertexCount ||
            chunkVertexCount > chunkSection.vertexCount - (chunkVertexStart - chunkSection.vertexStart)) {
            return false;
        }
        for (std::uint32_t offset = 0U; offset < chunkMeshlets; ++offset) {
            const std::size_t entry = kChunkHeaderBytes + static_cast<std::size_t>(offset) * kChunkClusterEntryBytes;
            const RenderMeshletDesc& meshlet = meshlets[firstMeshlet + offset];
            if (meshlet.sectionIndex != chunkSectionIndex ||
                meshlet.vertexStart != static_cast<std::uint64_t>(chunkVertexStart) + meshletVertexTotal ||
                meshlet.indexStart != static_cast<std::uint64_t>(chunkIndexStart) + meshletIndexTotal) {
                return false;
            }
            // The chunk states every cluster's place RELATIVE to itself; those statements have
            // to be the same clusters the primary block's table describes globally.
            if (PeekUInt32(payload, entry + 0U) != meshlet.vertexStart - chunkVertexStart ||
                PeekUInt32(payload, entry + 4U) != meshlet.vertexCount ||
                PeekUInt32(payload, entry + 8U) != meshlet.indexStart - chunkIndexStart ||
                PeekUInt32(payload, entry + 12U) != meshlet.indexCount) {
                return false;
            }
            meshletVertexTotal += meshlet.vertexCount;
            meshletIndexTotal += meshlet.indexCount;
        }
        if (meshletVertexTotal != chunkVertexCount || meshletIndexTotal != chunkIndexCount) {
            return false;
        }
        decodedChunks.push_back(GeometryChunk{
            .firstMeshlet = firstMeshlet,
            .meshletCount = chunkMeshlets,
            .vertexStart = chunkVertexStart,
            .vertexCount = chunkVertexCount,
            .indexStart = chunkIndexStart,
            .indexCount = chunkIndexCount,
        });
        expectedChunkMeshlet += chunkMeshlets;
        expectedChunkVertex += chunkVertexCount;
        expectedChunkIndex += chunkIndexCount;
    }
    if (expectedChunkMeshlet != meshletCount || expectedChunkVertex != vertexCount ||
        expectedChunkIndex != indexCount) {
        return false;
    }

    std::uint64_t encodedInputBytes = primaryBlock.size();
    for (const std::vector<std::uint8_t>& chunk : chunks) {
        if (chunk.size() > kb::assets::bake::kMaxAssetPackBytes - encodedInputBytes) {
            return false;
        }
        encodedInputBytes += chunk.size();
    }
    if (vertexStorageBytes > std::numeric_limits<std::size_t>::max() ||
        indexStorageBytes > std::numeric_limits<std::size_t>::max() ||
        vertexStorageBytes > kb::assets::bake::kMaxAssetPackBytes - encodedInputBytes ||
        indexStorageBytes > kb::assets::bake::kMaxAssetPackBytes - encodedInputBytes - vertexStorageBytes) {
        return false;
    }

    // Decode directly into the final typed runtime buffers. Keeping a byte-vector copy and
    // allocating the typed vector afterwards doubled the live vertex storage and could exceed
    // wasm32's heap even though each individual buffer stayed below the pack ceiling.
    RenderMeshAssetData asset{};
    std::span<std::uint8_t> vertices;
    if (tangents) {
        asset.tangentVertices.resize(vertexCount);
        vertices = std::span<std::uint8_t>{
            reinterpret_cast<std::uint8_t*>(asset.tangentVertices.data()),
            static_cast<std::size_t>(vertexStorageBytes),
        };
    } else {
        asset.vertices.resize(vertexCount);
        vertices = std::span<std::uint8_t>{
            reinterpret_cast<std::uint8_t*>(asset.vertices.data()),
            static_cast<std::size_t>(vertexStorageBytes),
        };
    }
    if (indexWidthBytes == 2U) {
        asset.indices16.resize(indexCount);
    } else {
        asset.indices32.resize(indexCount);
    }
    for (std::uint32_t chunkIndex = 0U; chunkIndex < chunkCount; ++chunkIndex) {
        const GeometryChunk& chunk = decodedChunks[chunkIndex];
        const std::vector<std::uint8_t>& payload = chunks[chunkIndex];
        const std::size_t vertexDataOffset =
            kChunkHeaderBytes + static_cast<std::size_t>(chunk.meshletCount) * kChunkClusterEntryBytes;
        const std::size_t indexDataOffset = PeekUInt32(payload, 20U);
        if (meshopt_decodeVertexBuffer(
                vertices.data() + static_cast<std::size_t>(chunk.vertexStart) * vertexStride,
                chunk.vertexCount,
                vertexStride,
                payload.data() + vertexDataOffset,
                indexDataOffset - vertexDataOffset) != 0) {
            return false;
        }
        void* const decodedIndexDestination = indexWidthBytes == 2U
            ? static_cast<void*>(asset.indices16.data() + chunk.indexStart)
            : static_cast<void*>(asset.indices32.data() + chunk.indexStart);
        if (meshopt_decodeIndexBuffer(
                decodedIndexDestination,
                chunk.indexCount,
                indexWidthBytes,
                payload.data() + indexDataOffset,
                payload.size() - indexDataOffset) != 0) {
            return false;
        }

        for (std::uint32_t meshletOffset = 0U; meshletOffset < chunk.meshletCount; ++meshletOffset) {
            const RenderMeshletDesc& meshlet = meshlets[chunk.firstMeshlet + meshletOffset];
            const std::uint32_t localVertexStart = meshlet.vertexStart - chunk.vertexStart;
            const std::uint32_t localIndexStart = meshlet.indexStart - chunk.indexStart;
            for (std::uint32_t indexOffset = 0U; indexOffset < meshlet.indexCount; ++indexOffset) {
                const std::uint32_t relative = indexWidthBytes == 2U
                    ? asset.indices16[chunk.indexStart + localIndexStart + indexOffset]
                    : asset.indices32[chunk.indexStart + localIndexStart + indexOffset];
                // A chunk-local index is not enough: each cluster is independently cullable,
                // so none of its triangles may reach into another cluster in the same fragment.
                if (relative < localVertexStart || relative >= localVertexStart + meshlet.vertexCount) {
                    return false;
                }
                const std::uint32_t global = chunk.vertexStart + relative;
                const RenderMeshSectionDesc& section = sections[meshlet.sectionIndex];
                if (global < section.vertexStart || global - section.vertexStart >= section.vertexCount) {
                    return false;
                }
                const std::uint32_t sectionLocal = global - section.vertexStart;
                if (indexWidthBytes == 2U) {
                    if (sectionLocal > std::numeric_limits<std::uint16_t>::max()) {
                        return false;
                    }
                    asset.indices16[meshlet.indexStart + indexOffset] = static_cast<std::uint16_t>(sectionLocal);
                } else {
                    asset.indices32[meshlet.indexStart + indexOffset] = sectionLocal;
                }
            }
        }
    }
    for (std::size_t offset = 0U; offset < vertices.size(); offset += sizeof(float)) {
        if (!std::isfinite(PeekFloat(vertices, offset))) {
            return false;
        }
    }

    const float* const decodedPositions = reinterpret_cast<const float*>(vertices.data());
    for (RenderMeshletDesc& meshlet : meshlets) {
        std::array<std::uint32_t, kMaxClusterTriangles * 3U> widenedIndices{};
        const RenderMeshSectionDesc& section = sections[meshlet.sectionIndex];
        for (std::uint32_t offset = 0U; offset < meshlet.indexCount; ++offset) {
            const std::uint32_t local = indexWidthBytes == 2U
                ? asset.indices16[meshlet.indexStart + offset]
                : asset.indices32[meshlet.indexStart + offset];
            if (local >= section.vertexCount) {
                return false;
            }
            widenedIndices[offset] = section.vertexStart + local;
        }
        const meshopt_Bounds expected = meshopt_computeClusterBounds(
            widenedIndices.data(),
            meshlet.indexCount,
            decodedPositions,
            vertexCount,
            vertexStride);
        const RenderBoundsSphere expectedBounds{
            .center = { expected.center[0], expected.center[1], expected.center[2] },
            .radius = expected.radius,
        };
        if (!IsFiniteBounds(expectedBounds) || !BoundsMatch(meshlet.bounds, expectedBounds) ||
            !ConeMatches(meshlet.cone, expected)) {
            return false;
        }
        // Minor platform-level floating-point differences are tolerated above, but hostile
        // recorded metadata is never published to the renderer. The geometry-derived values
        // are the canonical runtime culling contract and cannot under-bound their own meshlet.
        meshlet.bounds = expectedBounds;
        meshlet.cone = { expected.cone_axis[0], expected.cone_axis[1], expected.cone_axis[2], expected.cone_cutoff };
    }
    for (std::uint32_t sectionIndex = 0U; sectionIndex < sectionCount; ++sectionIndex) {
        const RenderMeshSectionDesc& section = sections[sectionIndex];
        const RenderBoundsSphere expectedSectionBounds =
            BoundsOfVertices(vertices.data(), vertexStride, section.vertexStart, section.vertexCount);
        if (!IsFiniteBounds(expectedSectionBounds) ||
            !BoundsMatch(sections[sectionIndex].bounds, expectedSectionBounds)) {
            return false;
        }
        sections[sectionIndex].bounds = expectedSectionBounds;
    }

    const RenderBoundsSphere recordedAssetBounds{
        .center = { PeekFloat(primaryBlock, 48U), PeekFloat(primaryBlock, 52U), PeekFloat(primaryBlock, 56U) },
        .radius = PeekFloat(primaryBlock, 60U),
    };
    const RenderBoundsSphere expectedAssetBounds =
        BoundsOfVertices(vertices.data(), vertexStride, 0U, vertexCount);
    if (!IsFiniteBounds(recordedAssetBounds) || !IsFiniteBounds(expectedAssetBounds) ||
        !BoundsMatch(recordedAssetBounds, expectedAssetBounds)) {
        return false;
    }

    asset.sections = std::move(sections);
    asset.meshlets = std::move(meshlets);
    asset.lods = std::move(lods);
    asset.materialSlots = std::move(materialSlots);
    asset.materialNames = std::move(materialNames);
    asset.embeddedMaterials = std::move(embeddedMaterials);
    asset.bounds = expectedAssetBounds;
    out = std::move(asset);
    return true;
}

bool ReadBakedMesh(
    std::span<const std::uint8_t> primaryBlock,
    std::span<const std::vector<std::uint8_t>> chunks,
    RenderMeshAssetData& out) {
    try {
        return ReadBakedMeshImpl(primaryBlock, chunks, out);
    } catch (const std::bad_alloc&) {
        return false;
    } catch (const std::length_error&) {
        return false;
    }
}

} // namespace kb::render::bake
