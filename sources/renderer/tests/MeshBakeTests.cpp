#include "RendererTestSupport.hpp"

#include "engine/assets/AssetManager.hpp"
#include "engine/assets/AssetMetadata.hpp"
#include "engine/assets/bake/AssetBakeKey.hpp"
#include "engine/assets/bake/AssetPackReader.hpp"
#include "engine/assets/bake/AssetPackWriter.hpp"
#include "engine/assets/bake/BakeTargetProfile.hpp"
#include "engine/assets/bake/BakedAssetSink.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneAssets.hpp"
#include "kb/render/bake/MeshBaker.hpp"
#include "kb/render/bake/TextureBaker.hpp"
#include "kb/render/resources/RenderMeshAssetLoader.hpp"
#include "kb/render/resources/RenderTextureAssetLoader.hpp"
#include "meshoptimizer/src/meshoptimizer.h"
#include "resources/RenderMeshAssetFinalizer.hpp"
#include "resources/RenderMeshDescValidator.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <cstdio>
#include <numbers>
#include <optional>
#include <span>
#include <string>
#include <system_error>
#include <typeindex>
#include <vector>

namespace kb::render::tests {
namespace {

using kb::assets::bake::AssetPackAccess;
using kb::assets::bake::AssetPackReader;
using kb::assets::bake::AssetPackReadStatus;
using kb::assets::bake::AssetPackWriter;
using kb::assets::bake::BakedAssetBlock;
using kb::assets::bake::BakedAssetDescriptor;
using kb::assets::bake::BakedAssetSinkStatus;
using kb::assets::bake::BakeIndexWidth;
using kb::assets::bake::BakeTargetProfile;
using kb::assets::bake::ShaderBakeBackend;
using kb::assets::bake::ShaderBakeBackendBit;
using kb::assets::bake::TextureCompressionFamily;
using kb::assets::bake::TextureCompressionFamilyBit;
using kb::render::bake::BakeMesh;
using kb::render::bake::MeshBakeOutput;
using kb::render::bake::MeshBakeStatus;

// ---- The baked mesh layout, restated ------------------------------------------------------
//
// Deliberately a second, independent statement of the format rather than a call into the
// baker's own encoder: a test that asks the implementation where a field is cannot notice the
// implementation moving it, and a fixture that patches the wrong byte is the trap that has
// already caught somebody here. Every offset below is checked by MeshBakeFormatSelfCheck.
constexpr std::size_t kHeaderBytes = 72U;
constexpr std::size_t kLodEntryBytes = 28U;
constexpr std::size_t kSectionEntryBytes = 40U;
constexpr std::size_t kMeshletEntryBytes = 56U;
constexpr std::size_t kChunkEntryBytes = 28U;
constexpr std::size_t kMaterialSlotEntryBytes = 8U;
constexpr std::size_t kChunkHeaderBytes = 24U;
constexpr std::size_t kChunkClusterEntryBytes = 16U;

// The limits the baker is supposed to hold to, written as literals. They must NOT be the
// baker's constants: an injection that widens the constant would widen the assertion with it.
constexpr std::uint32_t kClusterVertexLimit = 64U;
constexpr std::uint32_t kClusterTriangleLimit = 124U;
constexpr std::uint32_t kSixteenBitVertexLimit = 65536U;

[[nodiscard]] std::uint32_t ReadUInt32(std::span<const std::uint8_t> bytes, std::size_t offset) {
    Require(offset + 4U <= bytes.size(), "A baked mesh field was read past the end of its block");
    std::uint32_t value = 0U;
    for (std::uint32_t index = 0U; index < 4U; ++index) {
        value |= static_cast<std::uint32_t>(bytes[offset + index]) << (index * 8U);
    }
    return value;
}

[[nodiscard]] float ReadFloat(std::span<const std::uint8_t> bytes, std::size_t offset) {
    return std::bit_cast<float>(ReadUInt32(bytes, offset));
}

void WriteUInt32(std::vector<std::uint8_t>& bytes, std::size_t offset, std::uint32_t value) {
    Require(offset + 4U <= bytes.size(), "A baked mesh fixture wrote past the end of its block");
    for (std::uint32_t index = 0U; index < 4U; ++index) {
        bytes[offset + index] = static_cast<std::uint8_t>((value >> (index * 8U)) & 0xFFU);
    }
}

void WriteFloat(std::vector<std::uint8_t>& bytes, std::size_t offset, float value) {
    WriteUInt32(bytes, offset, std::bit_cast<std::uint32_t>(value));
}

void AppendUInt32(std::vector<std::uint8_t>& bytes, std::uint32_t value) {
    for (std::uint32_t index = 0U; index < 4U; ++index) {
        bytes.push_back(static_cast<std::uint8_t>((value >> (index * 8U)) & 0xFFU));
    }
}

void AppendFloat(std::vector<std::uint8_t>& bytes, float value) {
    AppendUInt32(bytes, std::bit_cast<std::uint32_t>(value));
}

struct BakedMeshView {
    std::uint32_t formatVersion = 0U;
    std::uint32_t vertexFormat = 0U;
    std::uint32_t vertexStride = 0U;
    std::uint32_t indexWidthBytes = 0U;
    std::uint32_t vertexCount = 0U;
    std::uint32_t indexCount = 0U;
    std::uint32_t lodCount = 0U;
    std::uint32_t sectionCount = 0U;
    std::uint32_t meshletCount = 0U;
    std::uint32_t chunkCount = 0U;
    std::uint32_t materialSlotCount = 0U;
    std::uint32_t materialMetadataBytes = 0U;
    std::array<float, 3> boundsCenter{};
    float boundsRadius = 0.0F;

    [[nodiscard]] std::size_t LodOffset(std::uint32_t index) const {
        return kHeaderBytes + materialSlotCount * kMaterialSlotEntryBytes + materialMetadataBytes +
            index * kLodEntryBytes;
    }
    [[nodiscard]] std::size_t SectionOffset(std::uint32_t index) const {
        return LodOffset(lodCount) + index * kSectionEntryBytes;
    }
    [[nodiscard]] std::size_t MeshletOffset(std::uint32_t index) const {
        return SectionOffset(sectionCount) + index * kMeshletEntryBytes;
    }
    [[nodiscard]] std::size_t ChunkOffset(std::uint32_t index) const {
        return MeshletOffset(meshletCount) + index * kChunkEntryBytes;
    }
    [[nodiscard]] std::size_t TotalBytes() const {
        return ChunkOffset(chunkCount);
    }
};

[[nodiscard]] BakedMeshView ViewBakedMesh(std::span<const std::uint8_t> primary) {
    Require(primary.size() >= kHeaderBytes, "A baked mesh primary block was shorter than its header");
    Require(std::memcmp(primary.data(), "21KBMESH", 8U) == 0, "A baked mesh primary block did not open with its magic");
    BakedMeshView view{};
    view.formatVersion = ReadUInt32(primary, 8U);
    view.vertexFormat = ReadUInt32(primary, 12U);
    view.vertexStride = ReadUInt32(primary, 16U);
    view.indexWidthBytes = ReadUInt32(primary, 20U);
    view.vertexCount = ReadUInt32(primary, 24U);
    view.indexCount = ReadUInt32(primary, 28U);
    view.lodCount = ReadUInt32(primary, 32U);
    view.sectionCount = ReadUInt32(primary, 36U);
    view.meshletCount = ReadUInt32(primary, 40U);
    view.chunkCount = ReadUInt32(primary, 44U);
    view.boundsCenter = { ReadFloat(primary, 48U), ReadFloat(primary, 52U), ReadFloat(primary, 56U) };
    view.boundsRadius = ReadFloat(primary, 60U);
    view.materialSlotCount = ReadUInt32(primary, 64U);
    view.materialMetadataBytes = ReadUInt32(primary, 68U);
    return view;
}

struct LodRecord {
    std::uint32_t firstSection = 0U;
    std::uint32_t sectionCount = 0U;
    std::uint32_t firstMeshlet = 0U;
    std::uint32_t meshletCount = 0U;
    std::uint32_t indexCount = 0U;
    float absoluteError = 0.0F;
    float minScreenCoverage = 0.0F;
};

[[nodiscard]] LodRecord ReadLod(std::span<const std::uint8_t> primary, const BakedMeshView& view, std::uint32_t index) {
    const std::size_t at = view.LodOffset(index);
    return LodRecord{
        .firstSection = ReadUInt32(primary, at + 0U),
        .sectionCount = ReadUInt32(primary, at + 4U),
        .firstMeshlet = ReadUInt32(primary, at + 8U),
        .meshletCount = ReadUInt32(primary, at + 12U),
        .indexCount = ReadUInt32(primary, at + 16U),
        .absoluteError = ReadFloat(primary, at + 20U),
        .minScreenCoverage = ReadFloat(primary, at + 24U),
    };
}

struct MeshletRecord {
    std::uint32_t indexStart = 0U;
    std::uint32_t indexCount = 0U;
    std::uint32_t vertexStart = 0U;
    std::uint32_t vertexCount = 0U;
    std::uint32_t sectionIndex = 0U;
    std::uint32_t lodLevel = 0U;
    std::array<float, 3> center{};
    float radius = 0.0F;
    std::array<float, 4> cone{};
};

[[nodiscard]] MeshletRecord ReadMeshlet(
    std::span<const std::uint8_t> primary,
    const BakedMeshView& view,
    std::uint32_t index) {
    const std::size_t at = view.MeshletOffset(index);
    MeshletRecord record{};
    record.indexStart = ReadUInt32(primary, at + 0U);
    record.indexCount = ReadUInt32(primary, at + 4U);
    record.vertexStart = ReadUInt32(primary, at + 8U);
    record.vertexCount = ReadUInt32(primary, at + 12U);
    record.sectionIndex = ReadUInt32(primary, at + 16U);
    record.lodLevel = ReadUInt32(primary, at + 20U);
    record.center = { ReadFloat(primary, at + 24U), ReadFloat(primary, at + 28U), ReadFloat(primary, at + 32U) };
    record.radius = ReadFloat(primary, at + 36U);
    record.cone = { ReadFloat(primary, at + 40U),
        ReadFloat(primary, at + 44U),
        ReadFloat(primary, at + 48U),
        ReadFloat(primary, at + 52U) };
    return record;
}

struct ChunkRecord {
    std::uint32_t firstMeshlet = 0U;
    std::uint32_t meshletCount = 0U;
    std::uint32_t vertexStart = 0U;
    std::uint32_t vertexCount = 0U;
    std::uint32_t indexStart = 0U;
    std::uint32_t indexCount = 0U;
    std::uint32_t byteLength = 0U;
};

[[nodiscard]] ChunkRecord ReadChunk(
    std::span<const std::uint8_t> primary,
    const BakedMeshView& view,
    std::uint32_t index) {
    const std::size_t at = view.ChunkOffset(index);
    return ChunkRecord{
        .firstMeshlet = ReadUInt32(primary, at + 0U),
        .meshletCount = ReadUInt32(primary, at + 4U),
        .vertexStart = ReadUInt32(primary, at + 8U),
        .vertexCount = ReadUInt32(primary, at + 12U),
        .indexStart = ReadUInt32(primary, at + 16U),
        .indexCount = ReadUInt32(primary, at + 20U),
        .byteLength = ReadUInt32(primary, at + 24U),
    };
}

// ---- Sinks ---------------------------------------------------------------------------------

struct RecordedBlock {
    std::string name;
    kb::assets::bake::BakedAssetBlockResidency residency = kb::assets::bake::BakedAssetBlockResidency::Resident;
    std::uint32_t alignmentBytes = 0U;
    std::optional<kb::assets::bake::BakedAssetBlockFragment> fragment;
    std::vector<std::uint8_t> bytes;
};

// Records the protocol as well as the payload, so "the baker talks to the sink correctly" is an
// observation rather than an assumption. `refuseAt` makes it refuse one call, which is what
// turns every error path in the baker into something a test can reach.
class RecordingSink final : public kb::assets::bake::IBakedAssetSink {
public:
    enum class RefuseAt : std::uint8_t { Nothing, Begin, Primary, Auxiliary, Commit };

    explicit RecordingSink(RefuseAt refuseAt = RefuseAt::Nothing) noexcept
        : refuseAt_{ refuseAt } {}

    BakedAssetSinkStatus BeginAsset(const BakedAssetDescriptor& opened) override {
        calls.push_back("begin");
        if (refuseAt_ == RefuseAt::Begin) {
            return BakedAssetSinkStatus::InvalidKey;
        }
        descriptor = opened;
        ++beginCount;
        return BakedAssetSinkStatus::Success;
    }

    BakedAssetSinkStatus WritePrimaryBlock(std::span<const std::uint8_t> bytes, std::uint32_t alignmentBytes) override {
        calls.push_back("primary");
        if (refuseAt_ == RefuseAt::Primary) {
            return BakedAssetSinkStatus::WriteFailed;
        }
        primary = RecordedBlock{
            .name = "primary",
            .residency = kb::assets::bake::BakedAssetBlockResidency::Resident,
            .alignmentBytes = alignmentBytes,
            .fragment = std::nullopt,
            .bytes = { bytes.begin(), bytes.end() },
        };
        return BakedAssetSinkStatus::Success;
    }

    BakedAssetSinkStatus WriteAuxiliaryBlock(const BakedAssetBlock& block, std::span<const std::uint8_t> bytes) override {
        calls.push_back("auxiliary");
        if (refuseAt_ == RefuseAt::Auxiliary) {
            return BakedAssetSinkStatus::WriteFailed;
        }
        auxiliary.push_back(RecordedBlock{
            .name = std::string{ block.name },
            .residency = block.residency,
            .alignmentBytes = block.alignmentBytes,
            .fragment = block.fragment,
            .bytes = { bytes.begin(), bytes.end() },
        });
        return BakedAssetSinkStatus::Success;
    }

    BakedAssetSinkStatus CommitAsset() override {
        calls.push_back("commit");
        if (refuseAt_ == RefuseAt::Commit) {
            return BakedAssetSinkStatus::WriteFailed;
        }
        ++commitCount;
        return BakedAssetSinkStatus::Success;
    }

    void AbortAsset() noexcept override {
        calls.push_back("abort");
        ++abortCount;
    }

    BakedAssetDescriptor descriptor{};
    RecordedBlock primary{};
    std::vector<RecordedBlock> auxiliary;
    std::vector<std::string> calls;
    std::uint32_t beginCount = 0U;
    std::uint32_t commitCount = 0U;
    std::uint32_t abortCount = 0U;

private:
    RefuseAt refuseAt_ = RefuseAt::Nothing;
};

struct SectionRecord {
    std::uint32_t indexStart = 0U;
    std::uint32_t indexCount = 0U;
    std::uint32_t materialSlot = 0U;
    std::uint32_t lodLevel = 0U;
    std::uint32_t vertexStart = 0U;
    std::uint32_t vertexCount = 0U;
};

[[nodiscard]] SectionRecord ReadSection(
    std::span<const std::uint8_t> primary,
    const BakedMeshView& view,
    std::uint32_t index) {
    const std::size_t at = view.SectionOffset(index);
    return SectionRecord{
        .indexStart = ReadUInt32(primary, at + 0U),
        .indexCount = ReadUInt32(primary, at + 4U),
        .materialSlot = ReadUInt32(primary, at + 8U),
        .lodLevel = ReadUInt32(primary, at + 12U),
        .vertexStart = ReadUInt32(primary, at + 32U),
        .vertexCount = ReadUInt32(primary, at + 36U),
    };
}

class ConflictingBakedMeshLoader final : public kb::assets::IAssetLoader {
public:
    [[nodiscard]] std::string_view Type() const noexcept override {
        return "ConflictingRenderMesh";
    }

    [[nodiscard]] std::type_index PayloadType() const noexcept override {
        return typeid(RenderMeshAssetData);
    }

    [[nodiscard]] std::vector<std::string> Extensions() const override {
        return { std::string{ kb::assets::bake::kAssetPackFileExtension } };
    }

    [[nodiscard]] std::vector<std::string> BakedAssetTypes() const override {
        return { std::string{ kb::render::bake::kMeshBakedAssetTypeId } };
    }

    [[nodiscard]] kb::assets::AssetLoadResult Load(const kb::assets::AssetLoadRequest&) override {
        return kb::assets::AssetLoadResult{ .asset = {}, .error = "Deliberate conflicting loader" };
    }
};

class TempStore {
public:
    explicit TempStore(const char* name) {
        std::error_code error;
        root_ = std::filesystem::temp_directory_path() / name;
        std::filesystem::remove_all(root_, error);
        std::filesystem::create_directories(root_, error);
        Require(!error, "Mesh bake tests could not create a temporary store");
    }

    TempStore(const TempStore&) = delete;
    TempStore& operator=(const TempStore&) = delete;

    ~TempStore() {
        std::error_code error;
        std::filesystem::remove_all(root_, error);
    }

    [[nodiscard]] const std::filesystem::path& Root() const noexcept {
        return root_;
    }

private:
    std::filesystem::path root_;
};

// ---- Fixtures ------------------------------------------------------------------------------

[[nodiscard]] RenderStaticMeshVertexP3N3T4UV2 MakeVertex(
    float x, float y, float z, float nx, float ny, float nz, float u, float v) {
    RenderStaticMeshVertexP3N3T4UV2 vertex{};
    vertex.x = x;
    vertex.y = y;
    vertex.z = z;
    vertex.nx = nx;
    vertex.ny = ny;
    vertex.nz = nz;
    vertex.u = u;
    vertex.v = v;
    return vertex;
}

void FinishFixture(RenderMeshAssetData& mesh) {
    mesh.sections.push_back(RenderMeshSectionDesc{
        .indexStart = 0U,
        .indexCount = static_cast<std::uint32_t>(mesh.indices32.size()),
        .materialSlot = 0U,
    });
    mesh.materialSlots.push_back(RenderMaterialSlotDesc{ .defaultMaterialAssetId = 7U });
}

// A closed sphere: no topological border anywhere, so the simplifier is free to reduce it, and
// its normals point in every direction, so a cluster cone that is not derived from the geometry
// cannot possibly be right.
[[nodiscard]] RenderMeshAssetData MakeSphere(std::uint32_t segments, std::uint32_t rings, float radius) {
    RenderMeshAssetData mesh{};
    for (std::uint32_t ring = 0U; ring <= rings; ++ring) {
        const float phi = static_cast<float>(std::numbers::pi) * static_cast<float>(ring) / static_cast<float>(rings);
        for (std::uint32_t segment = 0U; segment <= segments; ++segment) {
            const float theta =
                2.0F * static_cast<float>(std::numbers::pi) * static_cast<float>(segment) / static_cast<float>(segments);
            const float nx = std::sin(phi) * std::cos(theta);
            const float ny = std::cos(phi);
            const float nz = std::sin(phi) * std::sin(theta);
            mesh.tangentVertices.push_back(MakeVertex(
                nx * radius,
                ny * radius,
                nz * radius,
                nx,
                ny,
                nz,
                static_cast<float>(segment) / static_cast<float>(segments),
                static_cast<float>(ring) / static_cast<float>(rings)));
        }
    }
    const std::uint32_t stride = segments + 1U;
    for (std::uint32_t ring = 0U; ring < rings; ++ring) {
        for (std::uint32_t segment = 0U; segment < segments; ++segment) {
            const std::uint32_t a = ring * stride + segment;
            const std::uint32_t b = a + stride;
            mesh.indices32.insert(mesh.indices32.end(), { a, b, a + 1U, a + 1U, b, b + 1U });
        }
    }
    FinishFixture(mesh);
    return mesh;
}

// A flat sheet whose shape never changes and whose attributes do. Every position is coplanar,
// so a purely geometric simplifier sees the same mesh however the normals and the texture
// coordinates are set; whatever an error metric charges for the difference has to have come
// from the attributes.
//
// The fields are sinusoids rather than ramps on purpose: an attribute that is a linear function
// of position is reproduced exactly by interpolation, so a collapse across it costs nothing and
// a fixture built from one would prove nothing about the metric.
[[nodiscard]] RenderMeshAssetData MakeFlatSheet(std::uint32_t cells, float uvDetail, float normalDetail) {
    RenderMeshAssetData mesh{};
    for (std::uint32_t z = 0U; z <= cells; ++z) {
        for (std::uint32_t x = 0U; x <= cells; ++x) {
            const float fx = static_cast<float>(x) / static_cast<float>(cells);
            const float fz = static_cast<float>(z) / static_cast<float>(cells);
            const float nx = normalDetail * std::sin(fx * 30.0F);
            const float nz = normalDetail * std::cos(fz * 30.0F);
            const float ny = std::sqrt(std::max(0.0F, 1.0F - nx * nx - nz * nz));
            mesh.tangentVertices.push_back(MakeVertex(
                fx * 4.0F,
                0.0F,
                fz * 4.0F,
                nx,
                ny,
                nz,
                0.25F + uvDetail * std::sin(fx * 30.0F),
                0.25F + uvDetail * std::cos(fz * 30.0F)));
        }
    }
    const std::uint32_t stride = cells + 1U;
    for (std::uint32_t z = 0U; z < cells; ++z) {
        for (std::uint32_t x = 0U; x < cells; ++x) {
            const std::uint32_t a = z * stride + x;
            const std::uint32_t b = a + stride;
            mesh.indices32.insert(mesh.indices32.end(), { a, b, a + 1U, a + 1U, b, b + 1U });
        }
    }
    FinishFixture(mesh);
    return mesh;
}

void WriteUInt64(std::vector<std::uint8_t>& bytes, std::size_t offset, std::uint64_t value) {
    Require(offset + 8U <= bytes.size(), "A baked mesh fixture wrote past the end of its block");
    for (std::uint32_t index = 0U; index < 8U; ++index) {
        bytes[offset + index] = static_cast<std::uint8_t>((value >> (index * 8U)) & 0xFFU);
    }
}

[[nodiscard]] RenderMeshAssetData MakeSphereWithoutTangents(
    std::uint32_t segments,
    std::uint32_t rings,
    float radius) {
    RenderMeshAssetData tangent = MakeSphere(segments, rings, radius);
    RenderMeshAssetData mesh{};
    mesh.vertices.reserve(tangent.tangentVertices.size());
    for (const RenderStaticMeshVertexP3N3T4UV2& source : tangent.tangentVertices) {
        mesh.vertices.push_back(RenderStaticMeshVertexP3N3UV2{
            .x = source.x,
            .y = source.y,
            .z = source.z,
            .nx = source.nx,
            .ny = source.ny,
            .nz = source.nz,
            .u = source.u,
            .v = source.v,
            .u1 = source.u1,
            .v1 = source.v1,
            .r = source.r,
            .g = source.g,
            .b = source.b,
            .a = source.a,
        });
    }
    mesh.indices32 = std::move(tangent.indices32);
    mesh.sections = std::move(tangent.sections);
    mesh.materialSlots = std::move(tangent.materialSlots);
    return mesh;
}

enum class SecondaryDetail : std::uint8_t { Tangent, Uv1, Color };

[[nodiscard]] RenderMeshAssetData MakeSecondaryDetailSheet(std::uint32_t cells, SecondaryDetail detail) {
    RenderMeshAssetData mesh = MakeFlatSheet(cells, 0.0F, 0.0F);
    for (RenderStaticMeshVertexP3N3T4UV2& vertex : mesh.tangentVertices) {
        const float fx = vertex.x * 0.25F;
        const float fz = vertex.z * 0.25F;
        const float waveX = std::sin(fx * 30.0F);
        const float waveZ = std::cos(fz * 30.0F);
        switch (detail) {
        case SecondaryDetail::Tangent:
            vertex.tx = 0.5F * waveX;
            vertex.ty = std::sqrt(std::max(0.0F, 1.0F - vertex.tx * vertex.tx));
            vertex.tz = 0.5F * waveZ;
            vertex.tw = std::sin((fx + fz) * 24.0F);
            break;
        case SecondaryDetail::Uv1:
            vertex.u1 = waveX;
            vertex.v1 = waveZ;
            break;
        case SecondaryDetail::Color:
            vertex.r = 0.5F + 0.5F * waveX;
            vertex.g = 0.5F + 0.5F * waveZ;
            vertex.b = 0.5F + 0.5F * std::sin((fx + fz) * 24.0F);
            vertex.a = 0.5F + 0.5F * std::cos((fx - fz) * 24.0F);
            break;
        }
    }
    return mesh;
}

// Two materials over one surface, meeting along the middle column. The halves are simplified
// separately -- they are different sections -- so the vertices they share are the ones a LOD
// chain can crack apart, and this is the fixture that can see it happen.
[[nodiscard]] RenderMeshAssetData MakeTwoSectionSheet(std::uint32_t cells) {
    RenderMeshAssetData mesh{};
    for (std::uint32_t z = 0U; z <= cells; ++z) {
        for (std::uint32_t x = 0U; x <= cells; ++x) {
            const float fx = static_cast<float>(x) / static_cast<float>(cells);
            const float fz = static_cast<float>(z) / static_cast<float>(cells);
            mesh.tangentVertices.push_back(MakeVertex(
                fx * 4.0F,
                0.4F * std::sin(fx * 7.0F) * std::cos(fz * 7.0F),
                fz * 4.0F,
                0.0F,
                1.0F,
                0.0F,
                fx,
                fz));
        }
    }
    const std::uint32_t stride = cells + 1U;
    const std::uint32_t seam = cells / 2U;
    for (std::uint32_t half = 0U; half < 2U; ++half) {
        const std::uint32_t first = static_cast<std::uint32_t>(mesh.indices32.size());
        for (std::uint32_t z = 0U; z < cells; ++z) {
            for (std::uint32_t x = 0U; x < cells; ++x) {
                if ((x < seam) != (half == 0U)) {
                    continue;
                }
                const std::uint32_t a = z * stride + x;
                const std::uint32_t b = a + stride;
                mesh.indices32.insert(mesh.indices32.end(), { a, b, a + 1U, a + 1U, b, b + 1U });
            }
        }
        mesh.sections.push_back(RenderMeshSectionDesc{
            .indexStart = first,
            .indexCount = static_cast<std::uint32_t>(mesh.indices32.size()) - first,
            .materialSlot = half,
        });
    }
    mesh.materialSlots.push_back(RenderMaterialSlotDesc{ .defaultMaterialAssetId = 7U });
    mesh.materialSlots.push_back(RenderMaterialSlotDesc{ .defaultMaterialAssetId = 8U });
    return mesh;
}

// Disconnected triangles. Nothing is shared, so a cluster fills up on VERTICES long before it
// fills up on triangles, which is how a modest triangle count reaches past what a 16-bit index
// can address without a minute of simplification in a debug build.
[[nodiscard]] RenderMeshAssetData MakeTriangleSoup(std::uint32_t triangles) {
    RenderMeshAssetData mesh{};
    mesh.tangentVertices.reserve(triangles * 3U);
    mesh.indices32.reserve(triangles * 3U);
    for (std::uint32_t triangle = 0U; triangle < triangles; ++triangle) {
        const float column = static_cast<float>(triangle % 256U);
        const float row = static_cast<float>(triangle / 256U);
        const float nz = 1.0F;
        mesh.tangentVertices.push_back(MakeVertex(column, row, 0.0F, 0.0F, 0.0F, nz, 0.0F, 0.0F));
        mesh.tangentVertices.push_back(MakeVertex(column + 0.5F, row, 0.0F, 0.0F, 0.0F, nz, 1.0F, 0.0F));
        mesh.tangentVertices.push_back(MakeVertex(column, row + 0.5F, 0.0F, 0.0F, 0.0F, nz, 0.0F, 1.0F));
        mesh.indices32.push_back(triangle * 3U + 0U);
        mesh.indices32.push_back(triangle * 3U + 1U);
        mesh.indices32.push_back(triangle * 3U + 2U);
    }
    FinishFixture(mesh);
    return mesh;
}

[[nodiscard]] RenderMeshAssetData MakeSingleTriangle() {
    RenderMeshAssetData mesh{};
    mesh.tangentVertices.push_back(MakeVertex(0.0F, 0.0F, 0.0F, 0.0F, 0.0F, 1.0F, 0.0F, 0.0F));
    mesh.tangentVertices.push_back(MakeVertex(1.0F, 0.0F, 0.0F, 0.0F, 0.0F, 1.0F, 1.0F, 0.0F));
    mesh.tangentVertices.push_back(MakeVertex(0.0F, 1.0F, 0.0F, 0.0F, 0.0F, 1.0F, 0.0F, 1.0F));
    mesh.indices32 = { 0U, 1U, 2U };
    FinishFixture(mesh);
    return mesh;
}

[[nodiscard]] std::array<float, 36U> MaterialValues(const RenderMaterialDesc& desc) noexcept {
    return {
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
}

[[nodiscard]] std::array<std::uint64_t, 13U> MaterialTextureAssetIds(
    const RenderMaterialDesc& desc) noexcept {
    return {
        desc.albedoTextureAssetId,
        desc.normalTextureAssetId,
        desc.metallicRoughnessTextureAssetId,
        desc.occlusionTextureAssetId,
        desc.emissiveTextureAssetId,
        desc.clearcoatTextureAssetId,
        desc.clearcoatRoughnessTextureAssetId,
        desc.sheenColorTextureAssetId,
        desc.transmissionTextureAssetId,
        desc.thicknessTextureAssetId,
        desc.anisotropyTextureAssetId,
        desc.decalTextureAssetId,
        desc.layerMaskTextureAssetId,
    };
}

[[nodiscard]] std::array<std::string, 13U> MaterialTexturePaths(
    const RenderMeshEmbeddedMaterial& material) {
    return {
        material.albedoTexturePath,
        material.normalTexturePath,
        material.metallicRoughnessTexturePath,
        material.occlusionTexturePath,
        material.emissiveTexturePath,
        material.clearcoatTexturePath,
        material.clearcoatRoughnessTexturePath,
        material.sheenColorTexturePath,
        material.transmissionTexturePath,
        material.thicknessTexturePath,
        material.anisotropyTexturePath,
        material.decalTexturePath,
        material.layerMaskTexturePath,
    };
}

[[nodiscard]] RenderMeshEmbeddedMaterial MakeEmbeddedMaterialFixture() {
    RenderMeshEmbeddedMaterial material{};
    material.name = "Painted Copper";
    std::array<float*, 36U> values{
        &material.desc.baseColor[0], &material.desc.baseColor[1], &material.desc.baseColor[2],
        &material.desc.baseColor[3], &material.desc.emissiveColor[0], &material.desc.emissiveColor[1],
        &material.desc.emissiveColor[2], &material.desc.metallicFactor, &material.desc.roughnessFactor,
        &material.desc.normalScale, &material.desc.occlusionStrength, &material.desc.emissiveStrength,
        &material.desc.alphaCutoff, &material.desc.uvTiling[0], &material.desc.uvTiling[1],
        &material.desc.uvOffset[0], &material.desc.uvOffset[1], &material.desc.clearcoatFactor,
        &material.desc.clearcoatRoughnessFactor, &material.desc.sheenColor[0], &material.desc.sheenColor[1],
        &material.desc.sheenColor[2], &material.desc.sheenRoughnessFactor, &material.desc.transmissionFactor,
        &material.desc.thicknessFactor, &material.desc.attenuationColor[0], &material.desc.attenuationColor[1],
        &material.desc.attenuationColor[2], &material.desc.attenuationDistance,
        &material.desc.subsurfaceColor[0], &material.desc.subsurfaceColor[1],
        &material.desc.subsurfaceColor[2], &material.desc.subsurfaceFactor,
        &material.desc.anisotropyStrength, &material.desc.anisotropyRotation, &material.desc.layerWeight,
    };
    for (std::size_t index = 0U; index < values.size(); ++index) {
        *values[index] = 0.03125F * static_cast<float>(index + 1U);
    }
    material.desc.alphaMode = RenderMaterialAlphaMode::Blend;
    material.desc.decalBlendMode = RenderMaterialDecalBlendMode::Pbr;
    material.desc.layerBlendMode = RenderMaterialLayerBlendMode::Multiply;
    material.desc.translucencyBlend = RenderMaterialTranslucencyBlend::AlphaHoldout;
    material.desc.doubleSided = true;
    material.desc.writesDepth = false;
    std::array<std::uint64_t*, 13U> assetIds{
        &material.desc.albedoTextureAssetId,
        &material.desc.normalTextureAssetId,
        &material.desc.metallicRoughnessTextureAssetId,
        &material.desc.occlusionTextureAssetId,
        &material.desc.emissiveTextureAssetId,
        &material.desc.clearcoatTextureAssetId,
        &material.desc.clearcoatRoughnessTextureAssetId,
        &material.desc.sheenColorTextureAssetId,
        &material.desc.transmissionTextureAssetId,
        &material.desc.thicknessTextureAssetId,
        &material.desc.anisotropyTextureAssetId,
        &material.desc.decalTextureAssetId,
        &material.desc.layerMaskTextureAssetId,
    };
    for (std::size_t index = 0U; index < assetIds.size(); ++index) {
        *assetIds[index] = 1000U + index;
    }
    std::array<std::string*, 13U> paths{
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
    for (std::size_t index = 0U; index < paths.size(); ++index) {
        *paths[index] = "textures/channel_" + std::to_string(index) + ".png";
    }
    return material;
}

// Four triangles, closed, and already as small as a closed surface gets: there is no collapse
// that leaves a manifold behind, so the LOD chain has to stop at one level.
[[nodiscard]] RenderMeshAssetData MakeTetrahedron() {
    RenderMeshAssetData mesh{};
    const std::array<std::array<float, 3>, 4> corners{
        std::array<float, 3>{ 1.0F, 1.0F, 1.0F },
        std::array<float, 3>{ -1.0F, -1.0F, 1.0F },
        std::array<float, 3>{ -1.0F, 1.0F, -1.0F },
        std::array<float, 3>{ 1.0F, -1.0F, -1.0F },
    };
    for (const std::array<float, 3>& corner : corners) {
        const float length = std::sqrt(corner[0] * corner[0] + corner[1] * corner[1] + corner[2] * corner[2]);
        mesh.tangentVertices.push_back(MakeVertex(
            corner[0], corner[1], corner[2], corner[0] / length, corner[1] / length, corner[2] / length, 0.0F, 0.0F));
    }
    mesh.indices32 = { 0U, 1U, 2U, 0U, 3U, 1U, 0U, 2U, 3U, 1U, 3U, 2U };
    FinishFixture(mesh);
    return mesh;
}

// A profile whose only difference from a shipped one is the field a test is about. Built by
// hand so a test can bend one rule at a time without editing a shipped profile.
[[nodiscard]] BakeTargetProfile MakeProfile(
    std::string_view identifier,
    BakeIndexWidth indexWidth,
    std::uint64_t maxGeometryChunkBytes) {
    BakeTargetProfile profile = kb::assets::bake::WindowsX64BakeTargetProfile();
    profile.identifier = identifier;
    profile.indexWidth = indexWidth;
    profile.maxGeometryChunkBytes = maxGeometryChunkBytes;
    return profile;
}

[[nodiscard]] std::array<float, 3> VertexPosition(
    std::span<const std::uint8_t> chunkPayload,
    std::size_t vertexDataOffset,
    std::uint32_t vertexStride,
    std::uint32_t vertexIndex) {
    std::array<float, 3> position{};
    const std::size_t at = vertexDataOffset + static_cast<std::size_t>(vertexIndex) * vertexStride;
    Require(at + sizeof(position) <= chunkPayload.size(), "A chunk vertex was read past the end of its chunk");
    std::memcpy(position.data(), chunkPayload.data() + at, sizeof(position));
    return position;
}

// The vertices and indices of a bake, spliced back out of its chunks. Every test that wants to
// look at the geometry goes through here, so a chunk that lies about its contents shows up in
// all of them rather than only in the reader's own test.
struct SplicedGeometry {
    std::vector<std::array<float, 3>> positions;
    std::vector<std::uint32_t> indices;
};

[[nodiscard]] SplicedGeometry SpliceChunks(const MeshBakeOutput& output, const BakedMeshView& view) {
    SplicedGeometry geometry{};
    geometry.positions.assign(view.vertexCount, std::array<float, 3>{});
    geometry.indices.assign(view.indexCount, 0U);
    for (std::uint32_t chunkIndex = 0U; chunkIndex < view.chunkCount; ++chunkIndex) {
        const ChunkRecord chunk = ReadChunk(output.primaryBlock, view, chunkIndex);
        const std::vector<std::uint8_t>& payload = output.chunks[chunkIndex];
        const std::size_t vertexDataOffset = ReadUInt32(payload, 16U);
        const std::size_t indexDataOffset = ReadUInt32(payload, 20U);
        Require(vertexDataOffset <= indexDataOffset && indexDataOffset <= payload.size(),
            "An encoded chunk's stream offsets are out of order");

        std::vector<std::uint8_t> decodedVertices(
            static_cast<std::size_t>(chunk.vertexCount) * view.vertexStride);
        Require(meshopt_decodeVertexBuffer(
                    decodedVertices.data(),
                    chunk.vertexCount,
                    view.vertexStride,
                    payload.data() + vertexDataOffset,
                    indexDataOffset - vertexDataOffset) == 0,
            "The geometry fixture could not decode a baked vertex stream");

        std::vector<std::uint8_t> decodedIndices(
            static_cast<std::size_t>(chunk.indexCount) * view.indexWidthBytes);
        Require(meshopt_decodeIndexBuffer(
                    decodedIndices.data(),
                    chunk.indexCount,
                    view.indexWidthBytes,
                    payload.data() + indexDataOffset,
                    payload.size() - indexDataOffset) == 0,
            "The geometry fixture could not decode a baked index stream");
        for (std::uint32_t vertex = 0U; vertex < chunk.vertexCount; ++vertex) {
            geometry.positions[chunk.vertexStart + vertex] =
                VertexPosition(decodedVertices, 0U, view.vertexStride, vertex);
        }
        for (std::uint32_t index = 0U; index < chunk.indexCount; ++index) {
            const std::size_t at = static_cast<std::size_t>(index) * view.indexWidthBytes;
            const std::uint32_t relative = view.indexWidthBytes == 2U
                ? static_cast<std::uint32_t>(decodedIndices[at]) |
                      (static_cast<std::uint32_t>(decodedIndices[at + 1U]) << 8U)
                : ReadUInt32(decodedIndices, at);
            geometry.indices[chunk.indexStart + index] = chunk.vertexStart + relative;
        }
    }
    return geometry;
}

[[nodiscard]] MeshBakeOutput BakeInto(const RenderMeshAssetData& mesh, const BakeTargetProfile& profile) {
    RecordingSink sink;
    return BakeMesh(mesh, profile, sink);
}

enum class ReplayedFragmentMode : std::uint8_t { Omitted, WidenedBounds, WrongClusterCount };

[[nodiscard]] bool WriteReplayedMeshPack(
    const std::filesystem::path& path,
    const BakeTargetProfile& profile,
    const RecordingSink& recorded,
    ReplayedFragmentMode mode) {
    AssetPackWriter writer{ path, profile };
    if (writer.BeginAsset(recorded.descriptor) != BakedAssetSinkStatus::Success ||
        writer.WritePrimaryBlock(recorded.primary.bytes, recorded.primary.alignmentBytes) !=
            BakedAssetSinkStatus::Success) {
        return false;
    }
    for (const RecordedBlock& recordedBlock : recorded.auxiliary) {
        BakedAssetBlock block{};
        block.name = recordedBlock.name;
        block.residency = recordedBlock.residency;
        block.alignmentBytes = recordedBlock.alignmentBytes;
        if (mode != ReplayedFragmentMode::Omitted) {
            block.fragment = recordedBlock.fragment;
            if (!block.fragment.has_value()) return false;
            if (mode == ReplayedFragmentMode::WidenedBounds) {
                block.fragment->boundsMin[0] -= 1.0F;
            } else {
                ++block.fragment->clusterCount;
            }
        }
        if (writer.WriteAuxiliaryBlock(block, recordedBlock.bytes) != BakedAssetSinkStatus::Success) {
            return false;
        }
    }
    return writer.CommitAsset() == BakedAssetSinkStatus::Success &&
        writer.Finish() == BakedAssetSinkStatus::Success;
}

[[nodiscard]] bool MeshPackLoads(const std::filesystem::path& path) {
    kb::assets::AssetMetadata metadata{};
    metadata.type = "RenderMesh";
    metadata.virtualPath = path.filename();
    RenderMeshAssetLoader loader;
    return loader.Load(kb::assets::AssetLoadRequest{ metadata, path }).Succeeded();
}

} // namespace

// ---- The format the tests above assume is the format the baker writes ----------------------

void RunBakedMeshFormatSelfCheckTest() {
    const RenderMeshAssetData mesh = MakeSphere(16U, 12U, 1.0F);
    RecordingSink sink;
    const MeshBakeOutput output = BakeMesh(mesh, kb::assets::bake::WindowsX64BakeTargetProfile(), sink);
    Require(output.status == MeshBakeStatus::Success, "A sphere did not bake");

    const BakedMeshView view = ViewBakedMesh(output.primaryBlock);
    // If any of these disagree, every offset arithmetic in this file is aimed at the wrong
    // bytes and every fixture below would be patching something other than what it names.
    Require(view.formatVersion == 3U, "The baked mesh format version this suite reads is not the one baked");
    Require(view.vertexFormat == 1U, "A mesh with tangents did not bake as the tangent vertex layout");
    Require(view.vertexStride == sizeof(RenderStaticMeshVertexP3N3T4UV2),
        "The baked vertex stride is not the stride of the layout the header names");
    Require(view.TotalBytes() == output.primaryBlock.size(),
        "The table layout this suite computes is not the length of the primary block the baker wrote");
    Require(view.lodCount >= 1U && view.sectionCount >= 1U && view.meshletCount >= 1U && view.chunkCount >= 1U,
        "A baked sphere came back with an empty table");
    Require(output.chunks.size() == view.chunkCount, "The baker returned a different number of chunks than it declared");
    Require(view.materialSlotCount == 1U, "The baked mesh lost the material slot its source declared");
    Require(view.boundsRadius > 0.0F, "A baked sphere has no bounds");

    std::uint64_t encodedBytes = 0U;
    std::uint64_t uncompressedBytes = 0U;
    for (std::uint32_t chunkIndex = 0U; chunkIndex < view.chunkCount; ++chunkIndex) {
        const ChunkRecord chunk = ReadChunk(output.primaryBlock, view, chunkIndex);
        Require(chunk.byteLength == output.chunks[chunkIndex].size(),
            "A chunk's declared byte length is not the length of the chunk the baker produced");
        const std::vector<std::uint8_t>& payload = output.chunks[chunkIndex];
        Require(ReadUInt32(payload, 0U) == chunk.meshletCount && ReadUInt32(payload, 4U) == chunk.vertexCount &&
                ReadUInt32(payload, 8U) == chunk.indexCount && ReadUInt32(payload, 12U) == view.indexWidthBytes,
            "A chunk's own header disagrees with the chunk table that describes it");
        Require(ReadUInt32(payload, 16U) == kChunkHeaderBytes + chunk.meshletCount * kChunkClusterEntryBytes &&
                ReadUInt32(payload, 20U) > ReadUInt32(payload, 16U) &&
                ReadUInt32(payload, 20U) < payload.size(),
            "A chunk does not contain the two encoded streams after its cluster table");
        encodedBytes += payload.size();
        uncompressedBytes += kChunkHeaderBytes + chunk.meshletCount * kChunkClusterEntryBytes +
            static_cast<std::uint64_t>(chunk.vertexCount) * view.vertexStride +
            static_cast<std::uint64_t>(chunk.indexCount) * view.indexWidthBytes;
    }
    Require(encodedBytes < uncompressedBytes,
        "The geometry codec did not make the sphere's chunk payloads smaller than their raw layout");

    const MeshBakeOutput noTangents =
        BakeInto(MakeSphereWithoutTangents(16U, 12U, 1.0F), kb::assets::bake::WindowsX64BakeTargetProfile());
    Require(noTangents.status == MeshBakeStatus::Success, "A supported mesh without tangents did not bake");
    const BakedMeshView noTangentView = ViewBakedMesh(noTangents.primaryBlock);
    Require(noTangentView.vertexFormat == 0U &&
            noTangentView.vertexStride == sizeof(RenderStaticMeshVertexP3N3UV2),
        "A mesh without tangents did not preserve its declared runtime vertex layout");
    RenderMeshAssetData restored{};
    Require(kb::render::bake::ReadBakedMesh(noTangents.primaryBlock, noTangents.chunks, restored) &&
            restored.tangentVertices.empty() && restored.vertices.size() == noTangentView.vertexCount,
        "A baked mesh without tangents did not read back through its own layout");
}

// ---- Clusters ------------------------------------------------------------------------------

void RunBakedMeshClusterLimitsTest() {
    const RenderMeshAssetData mesh = MakeSphere(48U, 32U, 1.0F);
    const MeshBakeOutput output = BakeInto(mesh, kb::assets::bake::WindowsX64BakeTargetProfile());
    Require(output.status == MeshBakeStatus::Success, "A sphere did not bake");

    const BakedMeshView view = ViewBakedMesh(output.primaryBlock);
    Require(view.meshletCount > 8U,
        "A sphere of three thousand triangles produced too few clusters for the limits to mean anything");

    std::uint32_t coveredIndices = 0U;
    std::uint32_t coveredVertices = 0U;
    for (std::uint32_t index = 0U; index < view.meshletCount; ++index) {
        const MeshletRecord meshlet = ReadMeshlet(output.primaryBlock, view, index);
        Require(meshlet.vertexCount > 0U && meshlet.vertexCount <= kClusterVertexLimit,
            "A cluster carries more vertices than a cluster may have");
        Require(meshlet.indexCount > 0U && meshlet.indexCount % 3U == 0U,
            "A cluster does not hold whole triangles");
        Require(meshlet.indexCount / 3U <= kClusterTriangleLimit,
            "A cluster carries more triangles than a cluster may have");
        // The clusters partition the buffers: every index and every vertex belongs to exactly
        // one of them, in order. A cluster set that overlaps or leaves gaps would draw some
        // triangles twice and some never.
        Require(meshlet.indexStart == coveredIndices && meshlet.vertexStart == coveredVertices,
            "The clusters do not partition the baked buffers");
        coveredIndices += meshlet.indexCount;
        coveredVertices += meshlet.vertexCount;
    }
    Require(coveredIndices == view.indexCount && coveredVertices == view.vertexCount,
        "The clusters do not cover the whole baked buffer");
}

void RunBakedMeshClusterBoundsAndConeTest() {
    const RenderMeshAssetData mesh = MakeSphere(48U, 32U, 1.0F);
    const MeshBakeOutput output = BakeInto(mesh, kb::assets::bake::WindowsX64BakeTargetProfile());
    Require(output.status == MeshBakeStatus::Success, "A sphere did not bake");

    const BakedMeshView view = ViewBakedMesh(output.primaryBlock);
    const SplicedGeometry geometry = SpliceChunks(output, view);

    std::array<float, 4> firstCone{};
    std::array<float, 3> firstCenter{};
    bool coneVaries = false;
    bool centreVaries = false;
    float largestBaseClusterRadius = 0.0F;
    std::uint32_t narrowCones = 0U;
    for (std::uint32_t index = 0U; index < view.meshletCount; ++index) {
        const MeshletRecord meshlet = ReadMeshlet(output.primaryBlock, view, index);
        Require(meshlet.radius > 0.0F && std::isfinite(meshlet.radius), "A cluster has no bounding radius");
        if (meshlet.lodLevel == 0U) {
            // Only the base level: the coarsest LOD of a sphere can legitimately be one cluster
            // covering the whole mesh, and asserting tightness there would be asserting that the
            // chain does not go far enough.
            largestBaseClusterRadius = std::max(largestBaseClusterRadius, meshlet.radius);
        }
        if (index == 0U) {
            firstCenter = meshlet.center;
        } else if (std::fabs(meshlet.center[0] - firstCenter[0]) > 1e-3F ||
            std::fabs(meshlet.center[1] - firstCenter[1]) > 1e-3F ||
            std::fabs(meshlet.center[2] - firstCenter[2]) > 1e-3F) {
            centreVaries = true;
        }

        // Containment: whatever the sphere is, it has to be a sphere around THESE vertices.
        for (std::uint32_t offset = 0U; offset < meshlet.vertexCount; ++offset) {
            const std::array<float, 3>& position = geometry.positions[meshlet.vertexStart + offset];
            const float dx = position[0] - meshlet.center[0];
            const float dy = position[1] - meshlet.center[1];
            const float dz = position[2] - meshlet.center[2];
            Require(std::sqrt(dx * dx + dy * dy + dz * dz) <= meshlet.radius * 1.001F + 1e-4F,
                "A cluster's bounding sphere does not contain the cluster's own vertices");
        }

        const float axisLength = std::sqrt(
            meshlet.cone[0] * meshlet.cone[0] + meshlet.cone[1] * meshlet.cone[1] + meshlet.cone[2] * meshlet.cone[2]);
        if (index == 0U) {
            firstCone = meshlet.cone;
        } else if (std::fabs(meshlet.cone[0] - firstCone[0]) > 1e-3F ||
            std::fabs(meshlet.cone[1] - firstCone[1]) > 1e-3F ||
            std::fabs(meshlet.cone[2] - firstCone[2]) > 1e-3F) {
            coneVaries = true;
        }
        if (meshlet.cone[3] >= 1.0F) {
            // meshoptimizer says "this cluster has no usable cone" with a cutoff of one. That
            // is a legal answer, not a defaulted one, and it must not be counted as evidence.
            continue;
        }
        ++narrowCones;
        Require(std::fabs(axisLength - 1.0F) < 1e-2F, "A usable cluster cone does not have a unit axis");
        // The stored cutoff is the VIEW cone: meshoptimizer widens the normal cone by ninety
        // degrees on each side and inverts it, so cutoff = sin(a) where cos(a) is the tightest
        // dot between the axis and any triangle normal. Recovering cos(a) here is what makes
        // this a statement about these triangles rather than a restatement of the field.
        const float tightestDot = std::sqrt(std::max(0.0F, 1.0F - meshlet.cone[3] * meshlet.cone[3]));
        Require(tightestDot > 0.05F, "A cone declared usable is wider than meshoptimizer ever declares usable");
        // The cone is a backface-culling promise: every triangle in the cluster faces inside
        // it. A constant {0,0,1,1} cannot keep that promise on a sphere, and neither can a cone
        // computed from anything but these triangles.
        for (std::uint32_t corner = 0U; corner + 2U < meshlet.indexCount; corner += 3U) {
            const std::array<float, 3>& a = geometry.positions[geometry.indices[meshlet.indexStart + corner + 0U]];
            const std::array<float, 3>& b = geometry.positions[geometry.indices[meshlet.indexStart + corner + 1U]];
            const std::array<float, 3>& c = geometry.positions[geometry.indices[meshlet.indexStart + corner + 2U]];
            const std::array<float, 3> ab{ b[0] - a[0], b[1] - a[1], b[2] - a[2] };
            const std::array<float, 3> ac{ c[0] - a[0], c[1] - a[1], c[2] - a[2] };
            std::array<float, 3> normal{
                ab[1] * ac[2] - ab[2] * ac[1],
                ab[2] * ac[0] - ab[0] * ac[2],
                ab[0] * ac[1] - ab[1] * ac[0],
            };
            const float length = std::sqrt(normal[0] * normal[0] + normal[1] * normal[1] + normal[2] * normal[2]);
            if (length <= 0.0F) {
                continue;
            }
            for (float& component : normal) {
                component /= length;
            }
            const float alignment =
                normal[0] * meshlet.cone[0] + normal[1] * meshlet.cone[1] + normal[2] * meshlet.cone[2];
            Require(alignment >= tightestDot - 1e-2F,
                "A triangle of a cluster faces outside the cone the cluster claims to be inside");
        }
    }
    Require(coneVaries, "Every cluster of a sphere claims the same cone axis, which no real geometry would");
    Require(narrowCones * 2U > view.meshletCount,
        "Fewer than half the clusters of a sphere produced a usable cone, so the cone is not being derived");
    Require(centreVaries, "Every cluster claims the same bounding centre, which no real geometry would");
    Require(largestBaseClusterRadius < view.boundsRadius * 0.8F,
        "A base-level cluster is nearly as large as the whole mesh, so its bounds are the mesh's and not its own");
}

// ---- The LOD chain -------------------------------------------------------------------------

void RunBakedMeshLodChainTest() {
    const RenderMeshAssetData mesh = MakeSphere(48U, 32U, 1.0F);
    const MeshBakeOutput output = BakeInto(mesh, kb::assets::bake::WindowsX64BakeTargetProfile());
    Require(output.status == MeshBakeStatus::Success, "A sphere did not bake");

    const BakedMeshView view = ViewBakedMesh(output.primaryBlock);
    Require(view.lodCount > 1U, "A sphere of three thousand triangles produced no LOD chain at all");

    std::uint32_t previousIndexCount = 0U;
    float previousError = 0.0F;
    std::uint32_t coveredSections = 0U;
    std::uint32_t coveredMeshlets = 0U;
    for (std::uint32_t level = 0U; level < view.lodCount; ++level) {
        const LodRecord lod = ReadLod(output.primaryBlock, view, level);
        Require(lod.firstSection == coveredSections && lod.firstMeshlet == coveredMeshlets,
            "The LOD levels do not partition the section and cluster tables");
        coveredSections += lod.sectionCount;
        coveredMeshlets += lod.meshletCount;
        Require(lod.indexCount > 0U && lod.indexCount % 3U == 0U, "A LOD level does not hold whole triangles");
        if (level == 0U) {
            Require(lod.absoluteError == 0.0F, "The base LOD claims an error even though it is the original mesh");
        } else {
            Require(lod.indexCount < previousIndexCount, "A LOD level is not smaller than the level above it");
            Require(lod.absoluteError > previousError, "A LOD level records no more error than the level above it");
        }
        Require(lod.minScreenCoverage == 0.0F,
            "A LOD level carries a screen coverage, which cannot be computed without a camera");
        previousIndexCount = lod.indexCount;
        previousError = lod.absoluteError;
    }
    Require(coveredSections == view.sectionCount && coveredMeshlets == view.meshletCount,
        "The LOD levels do not cover the whole section and cluster tables");
}

void RunBakedMeshLodErrorIsAbsoluteTest() {
    // The same sphere at two sizes. An error measured against the mesh's extents would come out
    // the same for both; an absolute error is in the units the positions are in, so it has to
    // grow with the mesh. This is decision 3 stated as an experiment: nothing here is a
    // viewport or a field of view, and the number still has to mean a distance.
    const RenderMeshAssetData small = MakeSphere(48U, 32U, 1.0F);
    const RenderMeshAssetData large = MakeSphere(48U, 32U, 10.0F);
    const MeshBakeOutput smallOutput = BakeInto(small, kb::assets::bake::WindowsX64BakeTargetProfile());
    const MeshBakeOutput largeOutput = BakeInto(large, kb::assets::bake::WindowsX64BakeTargetProfile());
    Require(smallOutput.status == MeshBakeStatus::Success && largeOutput.status == MeshBakeStatus::Success,
        "A sphere did not bake at one of the two scales");

    const BakedMeshView smallView = ViewBakedMesh(smallOutput.primaryBlock);
    const BakedMeshView largeView = ViewBakedMesh(largeOutput.primaryBlock);
    Require(smallView.lodCount == largeView.lodCount && smallView.lodCount > 1U,
        "The same sphere at two scales produced different LOD chains");

    for (std::uint32_t level = 1U; level < smallView.lodCount; ++level) {
        const float smallError = ReadLod(smallOutput.primaryBlock, smallView, level).absoluteError;
        const float largeError = ReadLod(largeOutput.primaryBlock, largeView, level).absoluteError;
        Require(smallError > 0.0F && largeError > 0.0F, "A simplified LOD level recorded no error at all");
        const float ratio = largeError / smallError;
        Require(ratio > 7.0F && ratio < 14.0F,
            "A sphere ten times larger did not record roughly ten times the LOD error, so the error is relative");
    }
}

void RunBakedMeshSimplificationKeepsAttributesTest() {
    // Six sheets of exactly the same shape, with detail isolated in one channel at a time. A
    // simplifier that omits any channel cannot tell that sheet from the featureless one.
    //
    // The error is the measurement rather than the triangle count on purpose: the chain's
    // triangle target is reached in all three cases, so a count would say nothing, while the
    // recorded error is exactly what the attribute metric contributes.
    const BakeTargetProfile profile = kb::assets::bake::WindowsX64BakeTargetProfile();
    const MeshBakeOutput plain = BakeInto(MakeFlatSheet(48U, 0.0F, 0.0F), profile);
    const MeshBakeOutput texturedUv = BakeInto(MakeFlatSheet(48U, 1.0F, 0.0F), profile);
    const MeshBakeOutput texturedNormal = BakeInto(MakeFlatSheet(48U, 0.0F, 0.5F), profile);
    const MeshBakeOutput texturedTangent =
        BakeInto(MakeSecondaryDetailSheet(48U, SecondaryDetail::Tangent), profile);
    const MeshBakeOutput texturedUv1 = BakeInto(MakeSecondaryDetailSheet(48U, SecondaryDetail::Uv1), profile);
    const MeshBakeOutput texturedColor = BakeInto(MakeSecondaryDetailSheet(48U, SecondaryDetail::Color), profile);
    Require(plain.status == MeshBakeStatus::Success && texturedUv.status == MeshBakeStatus::Success &&
            texturedNormal.status == MeshBakeStatus::Success && texturedTangent.status == MeshBakeStatus::Success &&
            texturedUv1.status == MeshBakeStatus::Success && texturedColor.status == MeshBakeStatus::Success,
        "A flat sheet did not bake");

    const BakedMeshView plainView = ViewBakedMesh(plain.primaryBlock);
    const BakedMeshView uvView = ViewBakedMesh(texturedUv.primaryBlock);
    const BakedMeshView normalView = ViewBakedMesh(texturedNormal.primaryBlock);
    const BakedMeshView tangentView = ViewBakedMesh(texturedTangent.primaryBlock);
    const BakedMeshView uv1View = ViewBakedMesh(texturedUv1.primaryBlock);
    const BakedMeshView colorView = ViewBakedMesh(texturedColor.primaryBlock);
    Require(plainView.lodCount > 1U && uvView.lodCount == plainView.lodCount &&
            normalView.lodCount == plainView.lodCount && tangentView.lodCount == plainView.lodCount &&
            uv1View.lodCount == plainView.lodCount && colorView.lodCount == plainView.lodCount,
        "Sheets of one shape produced different LOD chains, so their errors are not comparable");

    const LodRecord plainLod = ReadLod(plain.primaryBlock, plainView, 1U);
    const LodRecord uvLod = ReadLod(texturedUv.primaryBlock, uvView, 1U);
    const LodRecord normalLod = ReadLod(texturedNormal.primaryBlock, normalView, 1U);
    const LodRecord tangentLod = ReadLod(texturedTangent.primaryBlock, tangentView, 1U);
    const LodRecord uv1Lod = ReadLod(texturedUv1.primaryBlock, uv1View, 1U);
    const LodRecord colorLod = ReadLod(texturedColor.primaryBlock, colorView, 1U);
    Require(plainLod.indexCount == uvLod.indexCount && plainLod.indexCount == normalLod.indexCount &&
            plainLod.indexCount == tangentLod.indexCount && plainLod.indexCount == uv1Lod.indexCount &&
            plainLod.indexCount == colorLod.indexCount,
        "Sheets of one shape simplified to different triangle counts, so the comparison is not of one shape");
    Require(plainLod.absoluteError > 0.0F, "The featureless sheet recorded no error at all");
    Require(uvLod.absoluteError > plainLod.absoluteError * 10.0F,
        "Simplifying a sheet with detailed texture coordinates cost no more than simplifying a blank one, "
        "so the texture coordinates are not in the error metric");
    Require(normalLod.absoluteError > plainLod.absoluteError * 10.0F,
        "Simplifying a sheet with detailed normals cost no more than simplifying a blank one, "
        "so the normals are not in the error metric");
    Require(tangentLod.absoluteError > plainLod.absoluteError * 1.5F,
        "Tangent detail is not in the simplifier's error metric");
    Require(uv1Lod.absoluteError > plainLod.absoluteError * 1.5F,
        "Secondary UV detail is not in the simplifier's error metric");
    Require(colorLod.absoluteError > plainLod.absoluteError * 1.5F,
        "Vertex color and alpha detail are not in the simplifier's error metric");
}

// ---- Index width, chunk budget -------------------------------------------------------------

void RunBakedMeshMaterialSeamTest() {
    // Two materials meeting along one line. Each section is simplified on its own, because a
    // cluster may not span two materials, and the vertices on the line belong to both -- so a
    // simplifier that is free to move them moves them differently on each side and leaves a
    // crack that nothing downstream can close.
    const RenderMeshAssetData mesh = MakeTwoSectionSheet(48U);
    const MeshBakeOutput output = BakeInto(mesh, kb::assets::bake::WindowsX64BakeTargetProfile());
    Require(output.status == MeshBakeStatus::Success, "A two-material sheet did not bake");

    const BakedMeshView view = ViewBakedMesh(output.primaryBlock);
    Require(view.materialSlotCount == 2U, "A two-material sheet lost a material slot");
    Require(view.lodCount > 1U, "A two-material sheet produced no LOD chain, so there is nothing to crack");
    const SplicedGeometry geometry = SpliceChunks(output, view);

    for (std::uint32_t level = 0U; level < view.lodCount; ++level) {
        const LodRecord lod = ReadLod(output.primaryBlock, view, level);
        Require(lod.sectionCount == 2U, "A LOD level of a two-material mesh does not have two sections");
        std::array<std::vector<std::array<float, 3>>, 2U> seamVertices{};
        std::array<std::uint32_t, 2U> materialSlots{};
        for (std::uint32_t offset = 0U; offset < lod.sectionCount; ++offset) {
            materialSlots[offset] = ReadUInt32(output.primaryBlock, view.SectionOffset(lod.firstSection + offset) + 8U);
        }
        Require(materialSlots[0] != materialSlots[1],
            "The two materials of a LOD level collapsed into one, so the boundary is gone");
        for (std::uint32_t index = lod.firstMeshlet; index < lod.firstMeshlet + lod.meshletCount; ++index) {
            const MeshletRecord meshlet = ReadMeshlet(output.primaryBlock, view, index);
            const std::uint32_t half = meshlet.sectionIndex - lod.firstSection;
            Require(half < 2U, "A cluster names a section outside its own LOD level");
            for (std::uint32_t vertex = 0U; vertex < meshlet.vertexCount; ++vertex) {
                const std::array<float, 3>& position = geometry.positions[meshlet.vertexStart + vertex];
                if (std::fabs(position[0] - 2.0F) < 1e-5F) {
                    seamVertices[half].push_back(position);
                }
            }
        }
        for (std::vector<std::array<float, 3>>& half : seamVertices) {
            std::ranges::sort(half);
            half.erase(std::unique(half.begin(), half.end()), half.end());
        }
        Require(!seamVertices[0].empty(), "A LOD level has no vertices on the boundary between its two materials");
        Require(seamVertices[0] == seamVertices[1],
            "The two materials of a LOD level do not agree about the vertices on their shared boundary, "
            "which is a crack down the middle of the mesh");
    }
}

void RunBakedMeshMaterialMetadataTest() {
    RenderMeshAssetData mesh = MakeSphere(16U, 12U, 1.0F);
    mesh.materialSlots[0].defaultMaterialAssetId = 0U;
    mesh.materialNames = { "Painted Copper" };
    mesh.embeddedMaterials = { MakeEmbeddedMaterialFixture() };
    const BakeTargetProfile profile = kb::assets::bake::WindowsX64BakeTargetProfile();
    const kb::assets::bake::AssetBakeKey originalKey = kb::render::bake::MakeMeshBakeKey(mesh, profile);

    RenderMeshAssetData recolored = mesh;
    recolored.embeddedMaterials[0].desc.baseColor[0] += 0.125F;
    Require(!(kb::render::bake::MakeMeshBakeKey(recolored, profile).Digest() == originalKey.Digest()),
        "Changing an embedded material left the mesh bake key unchanged");
    RenderMeshAssetData retextured = mesh;
    retextured.embeddedMaterials[0].normalTexturePath = "textures/a_different_normal.png";
    Require(!(kb::render::bake::MakeMeshBakeKey(retextured, profile).Digest() == originalKey.Digest()),
        "Changing an embedded texture path left the mesh bake key unchanged");
    RenderMeshAssetData renamed = mesh;
    renamed.materialNames[0] = "Renamed Slot";
    Require(!(kb::render::bake::MakeMeshBakeKey(renamed, profile).Digest() == originalKey.Digest()),
        "Changing a material name left the mesh bake key unchanged");

    const MeshBakeOutput baked = BakeInto(mesh, profile);
    Require(baked.status == MeshBakeStatus::Success, "A mesh with an embedded material did not bake");
    const BakedMeshView view = ViewBakedMesh(baked.primaryBlock);
    Require(view.materialMetadataBytes > 0U, "A baked mesh declared no material metadata payload");
    RenderMeshAssetData restored{};
    Require(kb::render::bake::ReadBakedMesh(baked.primaryBlock, baked.chunks, restored),
        "A baked mesh with material metadata did not read back");
    Require(restored.materialNames == mesh.materialNames && restored.embeddedMaterials.size() == 1U,
        "A baked mesh lost its material names or embedded material");
    const RenderMeshEmbeddedMaterial& expected = mesh.embeddedMaterials[0];
    const RenderMeshEmbeddedMaterial& actual = restored.embeddedMaterials[0];
    Require(actual.name == expected.name && MaterialValues(actual.desc) == MaterialValues(expected.desc) &&
            MaterialTextureAssetIds(actual.desc) == MaterialTextureAssetIds(expected.desc) &&
            actual.desc.alphaMode == expected.desc.alphaMode &&
            actual.desc.decalBlendMode == expected.desc.decalBlendMode &&
            actual.desc.layerBlendMode == expected.desc.layerBlendMode &&
            actual.desc.translucencyBlend == expected.desc.translucencyBlend &&
            actual.desc.doubleSided == expected.desc.doubleSided &&
            actual.desc.writesDepth == expected.desc.writesDepth &&
            MaterialTexturePaths(actual) == MaterialTexturePaths(expected),
        "An embedded material changed while passing through the baked mesh format");

    const std::size_t metadataOffset = kHeaderBytes + view.materialSlotCount * kMaterialSlotEntryBytes;
    {
        std::vector<std::uint8_t> damaged = baked.primaryBlock;
        WriteUInt64(damaged, metadataOffset, 2U);
        RenderMeshAssetData refused{};
        Require(!kb::render::bake::ReadBakedMesh(damaged, baked.chunks, refused),
            "Material names that no longer matched the slot count were accepted from disk");
    }
    {
        std::vector<std::uint8_t> damaged = baked.primaryBlock;
        WriteUInt64(damaged, metadataOffset + 16U, std::numeric_limits<std::uint64_t>::max());
        RenderMeshAssetData refused{};
        Require(!kb::render::bake::ReadBakedMesh(damaged, baked.chunks, refused),
            "An unbounded material-name length was accepted from disk");
    }
    {
        std::vector<std::uint8_t> damaged = baked.primaryBlock;
        const std::size_t descOffset = metadataOffset + 16U + 8U + mesh.materialNames[0].size() +
            8U + expected.name.size();
        WriteFloat(damaged, descOffset, std::numeric_limits<float>::quiet_NaN());
        RenderMeshAssetData refused{};
        Require(!kb::render::bake::ReadBakedMesh(damaged, baked.chunks, refused),
            "A non-finite embedded material value was accepted from disk");
    }

    TempStore store{ "kb_mesh_bake_materials" };
    const std::filesystem::path packPath = store.Root() / "material.kbpack";
    {
        AssetPackWriter writer{ packPath, profile };
        Require(BakeMesh(mesh, profile, writer).status == MeshBakeStatus::Success &&
                writer.Finish() == BakedAssetSinkStatus::Success,
            "A mesh with an embedded material did not publish to a package");
    }
    kb::assets::AssetMetadata metadata{};
    metadata.type = "RenderMesh";
    metadata.virtualPath = "material.kbpack";
    RenderMeshAssetLoader loader;
    const kb::assets::AssetLoadResult loadedResult =
        loader.Load(kb::assets::AssetLoadRequest{ metadata, packPath });
    Require(loadedResult.Succeeded(), "A package with an embedded material did not load");
    const std::shared_ptr<RenderMeshAssetData> loaded =
        std::static_pointer_cast<RenderMeshAssetData>(loadedResult.asset);
    Require(loaded->materialNames == mesh.materialNames && loaded->embeddedMaterials.size() == 1U &&
            MaterialValues(loaded->embeddedMaterials[0].desc) == MaterialValues(expected.desc) &&
            MaterialTexturePaths(loaded->embeddedMaterials[0]) == MaterialTexturePaths(expected),
        "The package loader lost the embedded material consumed by runtime rendering");

    RenderMeshAssetData misaligned = mesh;
    misaligned.materialNames.push_back("No such slot");
    RecordingSink misalignedSink;
    Require(BakeMesh(misaligned, profile, misalignedSink).status == MeshBakeStatus::MalformedMaterials &&
            misalignedSink.beginCount == 0U,
        "Material metadata that did not line up with slots reached the sink");
    RenderMeshAssetData transient = mesh;
    transient.embeddedMaterials[0].desc.albedoTexture.value = (1ULL << 32U) | 1ULL;
    RecordingSink transientSink;
    Require(BakeMesh(transient, profile, transientSink).status == MeshBakeStatus::MalformedMaterials &&
            transientSink.beginCount == 0U,
        "A process-local texture handle was serialized as baked content");
}

void RunBakedMeshIndexWidthTest() {
    // The production contract is one draw-local 16-bit index range per section. Sections share
    // one physical vertex buffer and carry the base range the renderer binds for that draw.
    const BakeTargetProfile narrow = MakeProfile("Mesh.Bits16", BakeIndexWidth::Bits16, 64ULL * 1024ULL * 1024ULL);
    const BakeTargetProfile wide = MakeProfile("Mesh.Bits32", BakeIndexWidth::Bits32, 64ULL * 1024ULL * 1024ULL);
    const RenderMeshAssetData tooLarge = MakeTriangleSoup(24000U);
    const MeshBakeOutput largeNarrow = BakeInto(tooLarge, narrow);
    const MeshBakeOutput largeWide = BakeInto(tooLarge, wide);
    Require(largeNarrow.status == MeshBakeStatus::Success && largeWide.status == MeshBakeStatus::Success,
        "A large mesh did not bake in both 16-bit and 32-bit profiles");
    const BakedMeshView largeNarrowView = ViewBakedMesh(largeNarrow.primaryBlock);
    Require(largeNarrowView.vertexCount > kSixteenBitVertexLimit && largeNarrowView.sectionCount > 1U,
        "A mesh above the 16-bit range was not split into independently addressable draw sections");

    bool foundRebasedSection = false;
    for (std::uint32_t sectionIndex = 0U; sectionIndex < largeNarrowView.sectionCount; ++sectionIndex) {
        const SectionRecord section = ReadSection(largeNarrow.primaryBlock, largeNarrowView, sectionIndex);
        Require(section.vertexCount > 0U && section.vertexCount <= kSixteenBitVertexLimit &&
                section.vertexStart < largeNarrowView.vertexCount &&
                section.vertexCount <= largeNarrowView.vertexCount - section.vertexStart,
            "A 16-bit draw section exceeded its addressable vertex range");
        foundRebasedSection = foundRebasedSection || section.vertexStart != 0U;
    }
    Require(foundRebasedSection, "The large 16-bit mesh did not contain a rebased draw section");

    const SplicedGeometry largeNarrowGeometry = SpliceChunks(largeNarrow, largeNarrowView);
    const BakedMeshView largeWideView = ViewBakedMesh(largeWide.primaryBlock);
    const SplicedGeometry largeWideGeometry = SpliceChunks(largeWide, largeWideView);
    Require(largeNarrowGeometry.indices.size() == largeWideGeometry.indices.size(),
        "Splitting a large 16-bit mesh changed its triangle count");
    for (std::size_t index = 0U; index < largeNarrowGeometry.indices.size(); ++index) {
        Require(largeNarrowGeometry.indices[index] < largeNarrowGeometry.positions.size() &&
                largeWideGeometry.indices[index] < largeWideGeometry.positions.size(),
            "A large baked mesh produced an out-of-range effective index");
        Require(largeNarrowGeometry.positions[largeNarrowGeometry.indices[index]] ==
                largeWideGeometry.positions[largeWideGeometry.indices[index]],
            "A large 16-bit section split changed the geometry relative to the 32-bit bake");
    }

    RenderMeshAssetData largeRestored{};
    Require(kb::render::bake::ReadBakedMesh(
                largeNarrow.primaryBlock, largeNarrow.chunks, largeRestored),
        "A split 16-bit mesh did not read back");
    Require(!largeRestored.indices16.empty() && largeRestored.indices32.empty(),
        "A split 16-bit mesh was widened while loading");
    Require(RenderMeshDescValidator::IsValid(largeRestored.RefreshDesc()),
        "A split 16-bit mesh read back into a runtime-invalid description");
    for (const RenderMeshSectionDesc& section : largeRestored.sections) {
        Require(section.vertexCount <= kSixteenBitVertexLimit,
            "A loaded 16-bit section exceeded the 16-bit contract");
        for (std::uint32_t offset = 0U; offset < section.indexCount; ++offset) {
            const std::uint32_t local = largeRestored.indices16[section.indexStart + offset];
            Require(local < section.vertexCount &&
                    section.vertexStart + local < largeRestored.tangentVertices.size(),
                "A loaded section-local index does not address its declared vertex range");
        }
    }

    const RenderMeshAssetData mesh = MakeTriangleSoup(4000U);
    const MeshBakeOutput narrowOutput = BakeInto(mesh, narrow);
    const MeshBakeOutput wideOutput = BakeInto(mesh, wide);
    Require(narrowOutput.status == MeshBakeStatus::Success && wideOutput.status == MeshBakeStatus::Success,
        "A triangle soup did not bake");

    const BakedMeshView narrowView = ViewBakedMesh(narrowOutput.primaryBlock);
    const BakedMeshView wideView = ViewBakedMesh(wideOutput.primaryBlock);
    Require(narrowView.indexWidthBytes == 2U, "A 16-bit profile did not bake 16-bit indices");
    Require(wideView.indexWidthBytes == 4U, "A 32-bit profile did not bake 32-bit indices");
    Require(narrowView.chunkCount > 1U && wideView.chunkCount > 1U,
        "A mesh with hundreds of clusters was not partitioned into topology-aware groups");

    for (std::uint32_t chunkIndex = 0U; chunkIndex < narrowView.chunkCount; ++chunkIndex) {
        const ChunkRecord chunk = ReadChunk(narrowOutput.primaryBlock, narrowView, chunkIndex);
        Require(chunk.vertexCount <= kSixteenBitVertexLimit,
            "A chunk holds more vertices than a 16-bit index can name");
    }

    // Split, not overflowed: the geometry that comes back has to be the geometry that went in.
    const SplicedGeometry narrowGeometry = SpliceChunks(narrowOutput, narrowView);
    const SplicedGeometry wideGeometry = SpliceChunks(wideOutput, wideView);
    Require(narrowGeometry.indices.size() == wideGeometry.indices.size(),
        "The two index widths produced different amounts of geometry");
    for (std::size_t index = 0U; index < narrowGeometry.indices.size(); ++index) {
        Require(narrowGeometry.indices[index] < narrowView.vertexCount,
            "A spliced 16-bit index does not name a vertex of the mesh");
        Require(narrowGeometry.positions[narrowGeometry.indices[index]] ==
                wideGeometry.positions[wideGeometry.indices[index]],
            "The 16-bit bake and the 32-bit bake do not describe the same triangle");
    }
    RenderMeshAssetData restored{};
    Require(kb::render::bake::ReadBakedMesh(narrowOutput.primaryBlock, narrowOutput.chunks, restored),
        "A section-addressable 16-bit bake did not read back");
    const RenderMeshDesc& desc = restored.RefreshDesc();
    Require(!restored.indices16.empty() && restored.indices32.empty() &&
            desc.indexFormat == RenderIndexFormat::Uint16,
        "A 16-bit baked profile was widened to a 32-bit runtime upload");
}

void RunSectionLocalTangentGenerationTest() {
    RenderMeshAssetData mesh{};
    mesh.vertices = {
        RenderStaticMeshVertexP3N3UV2{ .x = 0.0F, .y = 0.0F, .z = 0.0F, .ny = 0.0F, .nz = 1.0F, .u = 0.0F, .v = 0.0F },
        RenderStaticMeshVertexP3N3UV2{ .x = 1.0F, .y = 0.0F, .z = 0.0F, .ny = 0.0F, .nz = 1.0F, .u = 1.0F, .v = 0.0F },
        RenderStaticMeshVertexP3N3UV2{ .x = 0.0F, .y = 1.0F, .z = 0.0F, .ny = 0.0F, .nz = 1.0F, .u = 0.0F, .v = 1.0F },
        RenderStaticMeshVertexP3N3UV2{ .x = 10.0F, .y = 0.0F, .z = 0.0F, .ny = 0.0F, .nz = 1.0F, .u = 0.0F, .v = 0.0F },
        RenderStaticMeshVertexP3N3UV2{ .x = 11.0F, .y = 0.0F, .z = 0.0F, .ny = 0.0F, .nz = 1.0F, .u = 0.0F, .v = 1.0F },
        RenderStaticMeshVertexP3N3UV2{ .x = 10.0F, .y = 1.0F, .z = 0.0F, .ny = 0.0F, .nz = 1.0F, .u = 1.0F, .v = 0.0F },
    };
    mesh.indices16 = { 0U, 1U, 2U, 0U, 1U, 2U };
    mesh.sections = {
        RenderMeshSectionDesc{ .indexStart = 0U, .indexCount = 3U, .vertexStart = 0U, .vertexCount = 3U },
        RenderMeshSectionDesc{ .indexStart = 3U, .indexCount = 3U, .vertexStart = 3U, .vertexCount = 3U },
    };

    RenderMeshAssetFinalizer::EnsureTangentVertexStorage(mesh);
    Require(mesh.vertices.empty() && mesh.tangentVertices.size() == 6U,
        "Tangent generation did not preserve a section-local mesh's vertices");
    Require(mesh.tangentVertices[0].tx > 0.99F && std::abs(mesh.tangentVertices[0].ty) < 0.01F,
        "The first section produced the wrong tangent basis");
    Require(mesh.tangentVertices[3].ty > 0.99F && std::abs(mesh.tangentVertices[3].tx) < 0.01F,
        "Tangent generation ignored the second section's vertex base");
}

void RunSectionLocalIndexCompactionTest() {
    RenderMeshAssetData mesh{};
    mesh.vertices.resize(70'000U);
    mesh.vertices[0] = RenderStaticMeshVertexP3N3UV2{ .x = 0.0F, .y = 0.0F, .z = 0.0F, .nz = 1.0F };
    mesh.vertices[1] = RenderStaticMeshVertexP3N3UV2{ .x = 1.0F, .y = 0.0F, .z = 0.0F, .nz = 1.0F };
    mesh.vertices[2] = RenderStaticMeshVertexP3N3UV2{ .x = 0.0F, .y = 1.0F, .z = 0.0F, .nz = 1.0F };
    mesh.indices16 = { 0U, 0U, 0U }; // Stale alternate storage must not win RefreshDesc.
    mesh.indices32 = { 0U, 1U, 2U };
    mesh.sections = {
        RenderMeshSectionDesc{
            .indexStart = 0U,
            .indexCount = 3U,
            .vertexStart = 0U,
            .vertexCount = 70'000U,
        },
    };

    Require(RenderMeshAssetFinalizer::Finalize(mesh, RenderMeshFinalizeOptions{ .optimizeVertexFetch = false }),
        "Finalizing a valid wide section failed");
    Require(mesh.indices16.empty() && mesh.indices32.size() == 3U &&
            mesh.desc.indexFormat == RenderIndexFormat::Uint32,
        "Finalization narrowed a section that cannot be represented by the 16-bit section contract");
    Require(RenderMeshDescValidator::IsValid(mesh.desc),
        "Finalization reported success but produced an invalid mesh description");
}

void RunSectionLocalBoundsRefreshTest() {
    RenderMeshAssetData mesh{};
    mesh.vertices = {
        RenderStaticMeshVertexP3N3UV2{ .x = 0.0F, .y = 0.0F, .z = 0.0F, .nz = 1.0F },
        RenderStaticMeshVertexP3N3UV2{ .x = 1.0F, .y = 0.0F, .z = 0.0F, .nz = 1.0F },
        RenderStaticMeshVertexP3N3UV2{ .x = 0.0F, .y = 1.0F, .z = 0.0F, .nz = 1.0F },
        RenderStaticMeshVertexP3N3UV2{ .x = 10.0F, .y = 0.0F, .z = 0.0F, .nz = 1.0F },
        RenderStaticMeshVertexP3N3UV2{ .x = 11.0F, .y = 0.0F, .z = 0.0F, .nz = 1.0F },
        RenderStaticMeshVertexP3N3UV2{ .x = 10.0F, .y = 1.0F, .z = 0.0F, .nz = 1.0F },
    };
    mesh.indices32 = { 0U, 1U, 2U, 0U, 1U, 2U };
    const RenderBoundsSphere staleBounds{ .center = { 0.0F, 0.0F, 0.0F }, .radius = 1.0F };
    mesh.sections = {
        RenderMeshSectionDesc{
            .indexStart = 0U,
            .indexCount = 3U,
            .vertexStart = 0U,
            .vertexCount = 3U,
            .bounds = staleBounds,
        },
        RenderMeshSectionDesc{
            .indexStart = 3U,
            .indexCount = 3U,
            .vertexStart = 3U,
            .vertexCount = 3U,
            .bounds = staleBounds,
        },
    };

    Require(RenderMeshAssetFinalizer::Finalize(mesh, RenderMeshFinalizeOptions{ .optimizeVertexFetch = false }),
        "Finalizing section-local bounds failed");
    Require(mesh.sections[1].bounds.center[0] > 9.9F,
        "Finalization trusted stale bounds for a rebased section");
    Require(mesh.bounds.center[0] > 4.0F && mesh.bounds.radius > 5.0F,
        "Finalized mesh bounds do not cover both section-local vertex ranges");
}

void RunBakedMeshChunkBudgetTest() {
    const RenderMeshAssetData mesh = MakeSphere(48U, 32U, 1.0F);
    const std::uint64_t budget = 32768U;
    const BakeTargetProfile profile = MakeProfile("Mesh.SmallChunks", BakeIndexWidth::Bits32, budget);
    RecordingSink sink;
    const MeshBakeOutput output = BakeMesh(mesh, profile, sink);
    Require(output.status == MeshBakeStatus::Success, "A sphere did not bake into a small chunk budget");

    const BakedMeshView view = ViewBakedMesh(output.primaryBlock);
    Require(view.chunkCount > 4U, "A 32 KiB budget did not split a mesh of three thousand triangles");
    std::uint32_t coveredMeshlets = 0U;
    for (std::uint32_t chunkIndex = 0U; chunkIndex < view.chunkCount; ++chunkIndex) {
        const ChunkRecord chunk = ReadChunk(output.primaryBlock, view, chunkIndex);
        Require(chunk.byteLength <= budget, "A geometry chunk is larger than the profile's chunk budget");
        Require(chunk.meshletCount > 0U && chunk.meshletCount <= 10U,
            "A topology-aware group exceeded meshoptimizer's target-plus-one-third partition bound");
        Require(output.chunks[chunkIndex].size() <= budget,
            "A geometry chunk the baker produced is larger than the profile's chunk budget");
        Require(sink.auxiliary[chunkIndex].bytes.size() <= budget,
            "A geometry chunk handed to the sink is larger than the profile's chunk budget");
        // A chunk boundary is always a cluster boundary; that is what "a cluster group may not
        // straddle a fragment" is, seen from the baker.
        Require(chunk.firstMeshlet == coveredMeshlets, "The chunks do not partition the cluster list");
        coveredMeshlets += chunk.meshletCount;
    }
    Require(coveredMeshlets == view.meshletCount, "The chunks do not cover every cluster");

    // A budget below one cluster is a refusal, not a chunk that breaks it.
    RecordingSink tinySink;
    const MeshBakeOutput tiny = BakeMesh(mesh, MakeProfile("Mesh.NoChunks", BakeIndexWidth::Bits32, 256U), tinySink);
    Require(tiny.status == MeshBakeStatus::ChunkBudgetTooSmall,
        "A chunk budget that cannot hold one cluster was not refused");
    Require(tinySink.beginCount == 0U, "A refused bake still opened an artifact on the sink");
}

// ---- Determinism and the key ---------------------------------------------------------------

void RunBakedMeshDeterminismTest() {
    const RenderMeshAssetData first = MakeSphere(32U, 24U, 1.0F);
    const RenderMeshAssetData second = MakeSphere(32U, 24U, 1.0F);
    const BakeTargetProfile profile = kb::assets::bake::WindowsX64BakeTargetProfile();

    const MeshBakeOutput a = BakeInto(first, profile);
    const MeshBakeOutput b = BakeInto(first, profile);
    const MeshBakeOutput c = BakeInto(second, profile);
    Require(a.status == MeshBakeStatus::Success, "A sphere did not bake");
    Require(a.primaryBlock == b.primaryBlock && a.chunks == b.chunks,
        "Two bakes of one mesh did not produce the same bytes");
    Require(a.primaryBlock == c.primaryBlock && a.chunks == c.chunks,
        "Two separately built copies of one mesh did not bake to the same bytes");
    Require(a.key.Digest() == c.key.Digest(), "Two separately built copies of one mesh did not get the same key");

    const MeshBakeOutput android = BakeInto(first, kb::assets::bake::AndroidArm64BakeTargetProfile());
    Require(android.status == MeshBakeStatus::Success, "A sphere did not bake for the mobile profile");
    Require(!(android.key.Digest() == a.key.Digest()), "Two target profiles produced one bake key");
    Require(android.primaryBlock != a.primaryBlock, "Two target profiles produced identical bytes");
}

void RunBakedMeshKeyTest() {
    const RenderMeshAssetData mesh = MakeSphere(16U, 12U, 1.0F);
    const BakeTargetProfile profile = kb::assets::bake::WindowsX64BakeTargetProfile();
    const kb::assets::bake::AssetBakeKey key = kb::render::bake::MakeMeshBakeKey(mesh, profile);
    Require(key.IsValid(), "A mesh bake key is not valid");
    Require(key.bakerId == "Mesh" && key.bakerVersion == kb::render::bake::kMeshBakerVersion,
        "A mesh bake key does not name this baker");
    Require(key.settingsHash != 0U,
        "A mesh bake key carries no settings hash, so the cluster and LOD parameters are outside the key");
    {
        // The cluster and LOD parameters written out again here, as literals and in the order
        // the baker serialises them. Editing one of them in the baker has to move the key, and
        // this is what makes that falsifiable: a cluster size the key did not notice is a cache
        // that hands back geometry no baker would produce today. It is a restatement of the
        // encoding, not a derivation of it, so it catches a changed VALUE and not a changed
        // ENCODING -- the encoding is what kMeshBakerVersion is for.
        std::vector<std::uint8_t> parameters;
        AppendUInt32(parameters, 64U);
        AppendUInt32(parameters, 32U);
        AppendUInt32(parameters, 124U);
        AppendFloat(parameters, 0.25F);
        AppendFloat(parameters, 2.0F);
        AppendUInt32(parameters, 8U);
        AppendUInt32(parameters, 4U);
        AppendFloat(parameters, 0.5F);
        AppendFloat(parameters, 0.02F);
        AppendFloat(parameters, 1.0F);
        AppendFloat(parameters, 1.0F);
        AppendFloat(parameters, 0.5F);
        AppendFloat(parameters, 0.25F);
        Require(key.settingsHash == kb::assets::bake::HashBakeBytes(parameters),
            "The cluster and LOD parameters in the bake key are not the ones this suite was written against");
    }
    Require(key.targetProfileHash != 0U, "A mesh bake key carries no profile fingerprint");

    // Every field of the profile has to move the key even when the profile keeps its name --
    // the defect the fingerprint exists to close, restated for this baker.
    std::vector<BakeTargetProfile> edits;
    {
        BakeTargetProfile edited = profile;
        edited.textureCompressions |= TextureCompressionFamilyBit(TextureCompressionFamily::AdaptiveScalable);
        edits.push_back(edited);
    }
    {
        BakeTargetProfile edited = profile;
        edited.shaderBackends |= ShaderBakeBackendBit(ShaderBakeBackend::Glsl);
        edits.push_back(edited);
    }
    {
        BakeTargetProfile edited = profile;
        edited.indexWidth = BakeIndexWidth::Bits16;
        edits.push_back(edited);
    }
    {
        BakeTargetProfile edited = profile;
        edited.allowsThreeComponent16BitAttributes = false;
        edits.push_back(edited);
    }
    {
        BakeTargetProfile edited = profile;
        edited.packageBlockAlignmentBytes = 512U;
        edited.mappedBlockAlignmentBytes = 65536U;
        edits.push_back(edited);
    }
    {
        BakeTargetProfile edited = profile;
        edited.mappedBlockAlignmentBytes = 131072U;
        edits.push_back(edited);
    }
    {
        BakeTargetProfile edited = profile;
        edited.maxGeometryChunkBytes = profile.maxGeometryChunkBytes / 2U;
        edits.push_back(edited);
    }
    for (const BakeTargetProfile& edited : edits) {
        Require(edited.identifier == profile.identifier, "A profile edit changed the identifier as well");
        Require(!(kb::render::bake::MakeMeshBakeKey(mesh, edited).Digest() == key.Digest()),
            "Editing a target profile without renaming it left the mesh bake key unchanged");
    }
    // ...and a copy nobody edited must give the same key, or the rule above would be satisfied
    // by a key that simply moves whenever it is asked.
    const BakeTargetProfile untouched = profile;
    Require(kb::render::bake::MakeMeshBakeKey(mesh, untouched).Digest() == key.Digest(),
        "An unedited profile produced a different mesh bake key");

    RenderMeshAssetData moved = mesh;
    moved.tangentVertices[0].x += 0.5F;
    Require(!(kb::render::bake::MakeMeshBakeKey(moved, profile).Digest() == key.Digest()),
        "Moving a vertex left the mesh bake key unchanged");
    RenderMeshAssetData reslotted = mesh;
    reslotted.materialSlots[0].defaultMaterialAssetId = 11U;
    Require(!(kb::render::bake::MakeMeshBakeKey(reslotted, profile).Digest() == key.Digest()),
        "Changing a material slot left the mesh bake key unchanged");
}

// ---- The sink contract ---------------------------------------------------------------------

void RunBakedMeshSinkContractTest() {
    const RenderMeshAssetData mesh = MakeSphere(32U, 24U, 1.0F);
    const BakeTargetProfile profile = MakeProfile("Mesh.SinkContract", BakeIndexWidth::Bits32, 32768U);
    RecordingSink sink;
    const MeshBakeOutput output = BakeMesh(mesh, profile, sink);
    Require(output.status == MeshBakeStatus::Success, "A sphere did not bake");

    const BakedMeshView view = ViewBakedMesh(output.primaryBlock);
    Require(sink.beginCount == 1U && sink.commitCount == 1U && sink.abortCount == 0U,
        "A successful bake did not open and commit exactly one artifact");
    Require(sink.calls.size() == 3U + view.chunkCount, "A successful bake made an unexpected number of sink calls");
    Require(sink.calls.front() == "begin" && sink.calls.back() == "commit",
        "A bake did not begin and commit its artifact in that order");
    Require(sink.calls[1] == "primary", "A bake wrote something before its primary block");
    Require(sink.descriptor.assetTypeId == "StaticMesh", "A baked mesh did not announce itself as a static mesh");
    Require(sink.descriptor.key.IsValid() && sink.descriptor.key.Digest() == output.key.Digest(),
        "The key handed to the sink is not the key the bake reports");
    Require(sink.primary.alignmentBytes == profile.packageBlockAlignmentBytes,
        "The primary block was not given the profile's package alignment");
    Require(sink.primary.bytes == output.primaryBlock, "The primary block handed to the sink is not the one returned");
    Require(sink.auxiliary.size() == view.chunkCount, "The baker wrote a different number of chunks than it declared");

    for (std::uint32_t chunkIndex = 0U; chunkIndex < view.chunkCount; ++chunkIndex) {
        const RecordedBlock& block = sink.auxiliary[chunkIndex];
        const ChunkRecord chunk = ReadChunk(output.primaryBlock, view, chunkIndex);
        Require(block.name == "geom" + std::to_string(chunkIndex), "A geometry chunk was written under another name");
        Require(block.residency == kb::assets::bake::BakedAssetBlockResidency::Streaming,
            "A geometry chunk was not declared as a streaming block");
        Require(block.alignmentBytes == profile.packageBlockAlignmentBytes,
            "A geometry chunk was not given the profile's package alignment");
        Require(block.bytes == output.chunks[chunkIndex], "A chunk handed to the sink is not the one returned");
        Require(block.fragment.has_value(), "A geometry chunk was not declared as a streaming fragment");
        Require(block.fragment->clusterCount == chunk.meshletCount,
            "A fragment declares a different number of cluster groups than its chunk holds");
        const SplicedGeometry geometry = SpliceChunks(output, view);
        for (std::uint32_t vertex = 0U; vertex < chunk.vertexCount; ++vertex) {
            const std::array<float, 3>& position = geometry.positions[chunk.vertexStart + vertex];
            for (std::size_t axis = 0U; axis < 3U; ++axis) {
                Require(position[axis] >= block.fragment->boundsMin[axis] &&
                        position[axis] <= block.fragment->boundsMax[axis],
                    "A fragment's box does not contain a vertex of its own chunk");
            }
        }
    }

    const std::array<RecordingSink::RefuseAt, 4U> refusals{
        RecordingSink::RefuseAt::Begin,
        RecordingSink::RefuseAt::Primary,
        RecordingSink::RefuseAt::Auxiliary,
        RecordingSink::RefuseAt::Commit,
    };
    for (const RecordingSink::RefuseAt refuseAt : refusals) {
        RecordingSink refusing{ refuseAt };
        const MeshBakeOutput refused = BakeMesh(mesh, profile, refusing);
        Require(refused.status == MeshBakeStatus::SinkRejected, "A sink refusal was not reported as one");
        Require(refusing.commitCount == 0U, "A bake whose sink refused still committed an artifact");
        if (refuseAt != RecordingSink::RefuseAt::Begin) {
            Require(refusing.abortCount == 1U, "A bake whose sink refused after BeginAsset did not abort the artifact");
        }
    }
}

// ---- Sources this baker must refuse or handle without pretending ----------------------------

void RunBakedMeshDegenerateSourceTest() {
    const BakeTargetProfile profile = kb::assets::bake::WindowsX64BakeTargetProfile();
    const RenderMeshAssetData good = MakeSphere(16U, 12U, 1.0F);

    struct Case {
        const char* name;
        RenderMeshAssetData mesh;
        MeshBakeStatus expected;
    };
    std::vector<Case> cases;
    cases.push_back(Case{ "an empty mesh", RenderMeshAssetData{}, MeshBakeStatus::EmptySource });
    {
        RenderMeshAssetData mesh = good;
        mesh.sections.clear();
        cases.push_back(Case{ "a mesh with no sections", std::move(mesh), MeshBakeStatus::EmptySource });
    }
    {
        RenderMeshAssetData mesh = good;
        mesh.indices32.clear();
        mesh.sections.clear();
        mesh.sections.push_back(RenderMeshSectionDesc{ .indexStart = 0U, .indexCount = 0U });
        cases.push_back(Case{ "a mesh with no indices", std::move(mesh), MeshBakeStatus::EmptySource });
    }
    {
        RenderMeshAssetData mesh = good;
        mesh.indices32.pop_back();
        cases.push_back(Case{ "an index buffer that is not whole triangles", std::move(mesh),
            MeshBakeStatus::MalformedIndices });
    }
    {
        RenderMeshAssetData mesh = good;
        mesh.indices16.assign(mesh.indices32.begin(), mesh.indices32.end());
        cases.push_back(Case{ "both index buffers populated", std::move(mesh), MeshBakeStatus::MalformedIndices });
    }
    {
        RenderMeshAssetData mesh = good;
        mesh.indices32[4] = static_cast<std::uint32_t>(mesh.tangentVertices.size());
        cases.push_back(Case{ "an index that names no vertex", std::move(mesh), MeshBakeStatus::MalformedIndices });
    }
    {
        RenderMeshAssetData mesh = good;
        mesh.tangentVertices[3].y = std::numeric_limits<float>::quiet_NaN();
        cases.push_back(Case{ "a position that is not a number", std::move(mesh), MeshBakeStatus::NonFiniteGeometry });
    }
    {
        RenderMeshAssetData mesh = good;
        mesh.tangentVertices[3].z = std::numeric_limits<float>::infinity();
        cases.push_back(Case{ "an infinite position", std::move(mesh), MeshBakeStatus::NonFiniteGeometry });
    }
    {
        RenderMeshAssetData mesh = good;
        mesh.tangentVertices[0].u = std::numeric_limits<float>::max();
        mesh.tangentVertices[1].u = -std::numeric_limits<float>::max();
        cases.push_back(Case{ "finite attributes whose simplifier metric overflows", std::move(mesh),
            MeshBakeStatus::NonFiniteGeometry });
    }
    {
        RenderMeshAssetData mesh = MakeSingleTriangle();
        for (RenderStaticMeshVertexP3N3T4UV2& vertex : mesh.tangentVertices) {
            vertex.x = 0.0F;
            vertex.y = 0.0F;
            vertex.z = 0.0F;
        }
        cases.push_back(Case{ "a triangle that is one point", std::move(mesh), MeshBakeStatus::DegenerateGeometry });
    }
    {
        RenderMeshAssetData mesh = good;
        mesh.sections.push_back(mesh.sections.front());
        cases.push_back(Case{ "two sections over the same triangles", std::move(mesh),
            MeshBakeStatus::MalformedSections });
    }
    {
        RenderMeshAssetData mesh = good;
        mesh.sections.front().indexCount += 3U;
        cases.push_back(Case{ "a section that runs past the index buffer", std::move(mesh),
            MeshBakeStatus::MalformedSections });
    }
    {
        RenderMeshAssetData mesh = good;
        mesh.sections.front().indexCount -= 3U;
        cases.push_back(Case{ "indices not owned by any section", std::move(mesh), MeshBakeStatus::MalformedSections });
    }
    {
        RenderMeshAssetData mesh = good;
        mesh.sections.front().materialSlot = static_cast<std::uint32_t>(mesh.materialSlots.size());
        cases.push_back(Case{ "a section naming no material slot", std::move(mesh),
            MeshBakeStatus::MalformedSections });
    }
    {
        RenderMeshAssetData mesh = good;
        mesh.materialSlots.clear();
        mesh.sections.front().materialSlot = std::numeric_limits<std::uint32_t>::max();
        cases.push_back(Case{ "an unbounded material slot with no slot table", std::move(mesh),
            MeshBakeStatus::MalformedSections });
    }
    {
        RenderMeshAssetData mesh = good;
        mesh.sections.front().lodLevel = 1U;
        cases.push_back(Case{ "a mesh that already carries authored LODs", std::move(mesh),
            MeshBakeStatus::UnsupportedSourceShape });
    }
    {
        RenderMeshAssetData mesh = good;
        mesh.sections.front().terrainLayerIndex = 2U;
        cases.push_back(Case{ "a terrain layer pass", std::move(mesh), MeshBakeStatus::UnsupportedSourceShape });
    }
    {
        RenderMeshAssetData mesh = MakeSingleTriangle();
        const float largest = std::numeric_limits<float>::max();
        mesh.tangentVertices[0].x = -largest;
        mesh.tangentVertices[1].x = largest;
        mesh.tangentVertices[2].y = largest;
        cases.push_back(Case{ "finite positions whose derived bounds overflow", std::move(mesh),
            MeshBakeStatus::NonFiniteGeometry });
    }

    for (const Case& testCase : cases) {
        RecordingSink sink;
        const MeshBakeOutput output = BakeMesh(testCase.mesh, profile, sink);
        if (output.status != testCase.expected) {
            std::fprintf(stderr, "the source that was not refused as expected: %s (%s)\n", testCase.name,
                std::string{ kb::render::bake::ToString(output.status) }.c_str());
        }
        Require(output.status == testCase.expected, "A source this baker must refuse was not refused as expected");
        Require(output.key.IsValid(), "A source-validation failure did not return its deterministic bake key");
        Require(sink.beginCount == 0U && sink.commitCount == 0U, "A refused source still reached the sink");
    }

    // ...and the same call on the same fixture without the damage must succeed, so none of the
    // refusals above is being produced by something other than the damage.
    RecordingSink control;
    Require(BakeMesh(good, profile, control).status == MeshBakeStatus::Success,
        "The undamaged fixture the refusal cases are built from does not bake");

    BakeTargetProfile broken = profile;
    broken.maxGeometryChunkBytes = 0U;
    RecordingSink brokenSink;
    Require(BakeMesh(good, broken, brokenSink).status == MeshBakeStatus::InvalidProfile,
        "A profile that is not bakeable was not refused");
    Require(brokenSink.beginCount == 0U, "A refused profile still reached the sink");

    broken = profile;
    broken.maxGeometryChunkBytes = kb::assets::bake::kMaxAssetPackBlockBytes + 1U;
    RecordingSink oversizedSink;
    Require(BakeMesh(good, broken, oversizedSink).status == MeshBakeStatus::InvalidProfile,
        "A geometry chunk budget larger than the package block ceiling was accepted");
    Require(oversizedSink.beginCount == 0U, "An oversized chunk profile still reached the sink");
}

void RunBakedMeshSmallestMeshesTest() {
    const BakeTargetProfile profile = kb::assets::bake::WindowsX64BakeTargetProfile();

    // One triangle is a mesh, not an error: one cluster, one level, and no pretence of a chain.
    const MeshBakeOutput single = BakeInto(MakeSingleTriangle(), profile);
    Require(single.status == MeshBakeStatus::Success, "A single triangle did not bake");
    const BakedMeshView singleView = ViewBakedMesh(single.primaryBlock);
    Require(singleView.lodCount == 1U, "A single triangle produced a LOD chain it cannot possibly support");
    Require(singleView.meshletCount == 1U && singleView.indexCount == 3U && singleView.chunkCount == 1U,
        "A single triangle did not bake to a single cluster in a single chunk");

    // A closed surface that cannot be reduced any further stops the chain rather than repeating
    // itself: four identical levels would be four times the geometry for nothing.
    const MeshBakeOutput tetrahedron = BakeInto(MakeTetrahedron(), profile);
    Require(tetrahedron.status == MeshBakeStatus::Success, "A tetrahedron did not bake");
    const BakedMeshView tetrahedronView = ViewBakedMesh(tetrahedron.primaryBlock);
    Require(tetrahedronView.lodCount == 1U,
        "A mesh that cannot be simplified further still produced more than one LOD level");
    Require(tetrahedronView.indexCount == 12U, "A tetrahedron did not bake its four triangles");
}

// ---- The reader treats the payload as hostile ----------------------------------------------

void RunBakedMeshReaderCapsResidentGeometryTest() {
    // A few MiB of internally coordinated catalogue entries used to reach a >170 MiB resize
    // before the deliberately invalid one-byte codec streams could be rejected. The resident
    // cap is checked from the header before any of those aggregate geometry buffers exist.
    constexpr std::uint32_t meshletCount = 48000U;
    constexpr std::uint32_t vertexCount = meshletCount * 64U;
    constexpr std::uint32_t indexCount = meshletCount * 3U;

    std::vector<std::uint8_t> chunk;
    const std::uint32_t vertexDataOffset = static_cast<std::uint32_t>(
        kChunkHeaderBytes + static_cast<std::size_t>(meshletCount) * kChunkClusterEntryBytes);
    const std::uint32_t indexDataOffset = vertexDataOffset + 1U;
    AppendUInt32(chunk, meshletCount);
    AppendUInt32(chunk, vertexCount);
    AppendUInt32(chunk, indexCount);
    AppendUInt32(chunk, 4U);
    AppendUInt32(chunk, vertexDataOffset);
    AppendUInt32(chunk, indexDataOffset);
    for (std::uint32_t meshlet = 0U; meshlet < meshletCount; ++meshlet) {
        AppendUInt32(chunk, meshlet * 64U);
        AppendUInt32(chunk, 64U);
        AppendUInt32(chunk, meshlet * 3U);
        AppendUInt32(chunk, 3U);
    }
    chunk.push_back(0U);
    chunk.push_back(0U);

    std::vector<std::uint8_t> primary;
    primary.insert(primary.end(), { '2', '1', 'K', 'B', 'M', 'E', 'S', 'H' });
    AppendUInt32(primary, 3U);
    AppendUInt32(primary, 0U);
    AppendUInt32(primary, sizeof(RenderStaticMeshVertexP3N3UV2));
    AppendUInt32(primary, 4U);
    AppendUInt32(primary, vertexCount);
    AppendUInt32(primary, indexCount);
    AppendUInt32(primary, 1U);
    AppendUInt32(primary, 1U);
    AppendUInt32(primary, meshletCount);
    AppendUInt32(primary, 1U);
    AppendFloat(primary, 0.0F);
    AppendFloat(primary, 0.0F);
    AppendFloat(primary, 0.0F);
    AppendFloat(primary, 1.0F);
    AppendUInt32(primary, 0U);
    AppendUInt32(primary, 0U);
    // One LOD, one section.
    AppendUInt32(primary, 0U);
    AppendUInt32(primary, 1U);
    AppendUInt32(primary, 0U);
    AppendUInt32(primary, meshletCount);
    AppendUInt32(primary, indexCount);
    AppendFloat(primary, 0.0F);
    AppendFloat(primary, 0.0F);
    AppendUInt32(primary, 0U);
    AppendUInt32(primary, indexCount);
    AppendUInt32(primary, 0U);
    AppendUInt32(primary, 0U);
    AppendFloat(primary, 0.0F);
    AppendFloat(primary, 0.0F);
    AppendFloat(primary, 0.0F);
    AppendFloat(primary, 1.0F);
    AppendUInt32(primary, 0U);
    AppendUInt32(primary, vertexCount);
    for (std::uint32_t meshlet = 0U; meshlet < meshletCount; ++meshlet) {
        AppendUInt32(primary, meshlet * 3U);
        AppendUInt32(primary, 3U);
        AppendUInt32(primary, meshlet * 64U);
        AppendUInt32(primary, 64U);
        AppendUInt32(primary, 0U);
        AppendUInt32(primary, 0U);
        AppendFloat(primary, 0.0F);
        AppendFloat(primary, 0.0F);
        AppendFloat(primary, 0.0F);
        AppendFloat(primary, 1.0F);
        AppendFloat(primary, 0.0F);
        AppendFloat(primary, 0.0F);
        AppendFloat(primary, 1.0F);
        AppendFloat(primary, 1.0F);
    }
    AppendUInt32(primary, 0U);
    AppendUInt32(primary, meshletCount);
    AppendUInt32(primary, 0U);
    AppendUInt32(primary, vertexCount);
    AppendUInt32(primary, 0U);
    AppendUInt32(primary, indexCount);
    AppendUInt32(primary, static_cast<std::uint32_t>(chunk.size()));

    RenderMeshAssetData refused{};
    const std::array<std::vector<std::uint8_t>, 1U> chunks{ std::move(chunk) };
    Require(!kb::render::bake::ReadBakedMesh(primary, chunks, refused),
        "A compact catalogue was allowed to request geometry larger than the resident runtime budget");
}

void RunBakedMeshReaderRefusesTamperedPayloadTest() {
    const RenderMeshAssetData mesh = MakeSphere(32U, 24U, 1.0F);
    const BakeTargetProfile profile = MakeProfile("Mesh.Reader", BakeIndexWidth::Bits32, 32768U);
    const MeshBakeOutput output = BakeInto(mesh, profile);
    Require(output.status == MeshBakeStatus::Success, "A sphere did not bake");
    const BakedMeshView view = ViewBakedMesh(output.primaryBlock);
    Require(view.chunkCount > 1U, "The reader fixture has one chunk, so its chunk rules cannot be reached");

    RenderMeshAssetData restored{};
    Require(kb::render::bake::ReadBakedMesh(output.primaryBlock, output.chunks, restored),
        "An untouched baked mesh did not read back");
    Require(restored.tangentVertices.size() == view.vertexCount && restored.indices32.size() == view.indexCount &&
            restored.meshlets.size() == view.meshletCount && restored.lods.size() == view.lodCount &&
            restored.sections.size() == view.sectionCount && restored.materialSlots.size() == view.materialSlotCount,
        "A baked mesh that read back does not have the shape it declared");
    Require(restored.materialSlots.front().defaultMaterialAssetId == 7U,
        "A baked mesh lost the material its source slot named");
    for (const RenderMeshletDesc& meshlet : restored.meshlets) {
        Require(meshlet.IsValid(), "A cluster that read back is not a valid cluster");
    }

    {
        const MeshBakeOutput large = BakeInto(MakeSphere(16U, 12U, 1'000'000.0F), profile);
        Require(large.status == MeshBakeStatus::Success, "The large-bounds fixture did not bake");
        const BakedMeshView largeView = ViewBakedMesh(large.primaryBlock);
        std::vector<std::uint8_t> slightlyUnderbounded = large.primaryBlock;
        const float recordedRadius = largeView.boundsRadius;
        const float underboundedRadius = recordedRadius * (1.0F - 5.0e-6F);
        WriteFloat(slightlyUnderbounded, 60U, underboundedRadius);
        RenderMeshAssetData canonical{};
        Require(kb::render::bake::ReadBakedMesh(slightlyUnderbounded, large.chunks, canonical),
            "A within-tolerance metadata difference was not handled as a platform float difference");
        Require(canonical.bounds.radius > underboundedRadius && canonical.bounds.radius == recordedRadius,
            "The reader published a tolerated under-bound instead of the geometry-derived canonical bound");
    }

    // Each case says what it damages, the value it expects to find there, and the value it
    // writes -- and the harness checks that the field really did change before it believes the
    // refusal. A fixture that patched the wrong offset would be caught here rather than passing
    // at whatever guard happened to fire first.
    struct PrimaryTamper {
        const char* name;
        std::size_t offset;
        std::uint32_t expectedBefore;
        std::uint32_t after;
    };
    std::vector<PrimaryTamper> tampers;
    tampers.push_back(PrimaryTamper{ "the format version", 8U, 3U, 4U });
    tampers.push_back(PrimaryTamper{ "material metadata bytes with no payload", 68U, 0U, 1U });
    tampers.push_back(PrimaryTamper{ "the vertex layout", 12U, 1U, 5U });
    tampers.push_back(PrimaryTamper{ "the vertex stride", 16U, view.vertexStride, view.vertexStride + 4U });
    tampers.push_back(PrimaryTamper{ "the index width", 20U, 4U, 3U });
    tampers.push_back(PrimaryTamper{ "the chunk count", 44U, view.chunkCount, view.chunkCount - 1U });
    tampers.push_back(PrimaryTamper{ "the cluster count", 40U, view.meshletCount, view.meshletCount - 1U });
    tampers.push_back(PrimaryTamper{ "a hostile vertex count", 24U, view.vertexCount,
        std::numeric_limits<std::uint32_t>::max() });
    tampers.push_back(PrimaryTamper{ "a hostile cluster-table count", 40U, view.meshletCount,
        std::numeric_limits<std::uint32_t>::max() });
    tampers.push_back(PrimaryTamper{ "the LOD count", 32U, view.lodCount, view.lodCount + 1U });
    tampers.push_back(PrimaryTamper{ "a LOD's index total",
        view.LodOffset(0U) + 16U,
        ReadLod(output.primaryBlock, view, 0U).indexCount,
        ReadLod(output.primaryBlock, view, 0U).indexCount + 3U });
    tampers.push_back(PrimaryTamper{ "a cluster's vertex count",
        view.MeshletOffset(0U) + 12U,
        ReadMeshlet(output.primaryBlock, view, 0U).vertexCount,
        kClusterVertexLimit + 1U });
    tampers.push_back(PrimaryTamper{ "a cluster's section",
        view.MeshletOffset(0U) + 16U,
        ReadMeshlet(output.primaryBlock, view, 0U).sectionIndex,
        view.sectionCount });
    tampers.push_back(PrimaryTamper{ "a chunk's first cluster",
        view.ChunkOffset(1U) + 0U,
        ReadChunk(output.primaryBlock, view, 1U).firstMeshlet,
        ReadChunk(output.primaryBlock, view, 1U).firstMeshlet + 1U });
    tampers.push_back(PrimaryTamper{ "a chunk's declared byte length",
        view.ChunkOffset(0U) + 24U,
        ReadChunk(output.primaryBlock, view, 0U).byteLength,
        ReadChunk(output.primaryBlock, view, 0U).byteLength + 4U });
    tampers.push_back(PrimaryTamper{ "a section's first index",
        view.SectionOffset(0U) + 0U,
        0U,
        3U });
    tampers.push_back(PrimaryTamper{ "a section's owning LOD",
        view.SectionOffset(0U) + 12U,
        0U,
        1U });
    tampers.push_back(PrimaryTamper{ "a section's first vertex",
        view.SectionOffset(0U) + 32U,
        ReadSection(output.primaryBlock, view, 0U).vertexStart,
        view.vertexCount });
    tampers.push_back(PrimaryTamper{ "a section's vertex count",
        view.SectionOffset(0U) + 36U,
        ReadSection(output.primaryBlock, view, 0U).vertexCount,
        std::numeric_limits<std::uint32_t>::max() });

    for (const PrimaryTamper& tamper : tampers) {
        std::vector<std::uint8_t> damaged = output.primaryBlock;
        Require(ReadUInt32(damaged, tamper.offset) == tamper.expectedBefore,
            "A baked mesh fixture did not find the field it names at the offset it patches");
        Require(tamper.after != tamper.expectedBefore, "A baked mesh fixture wrote the value that was already there");
        WriteUInt32(damaged, tamper.offset, tamper.after);
        Require(ReadUInt32(damaged, tamper.offset) == tamper.after, "A baked mesh fixture did not change the field");
        RenderMeshAssetData refused{};
        if (kb::render::bake::ReadBakedMesh(damaged, output.chunks, refused)) {
            std::fprintf(stderr, "the tampered field was: %s\n", tamper.name);
        }
        Require(!kb::render::bake::ReadBakedMesh(damaged, output.chunks, refused),
            "A tampered baked mesh was accepted");
    }

    {
        // A LOD chain whose error goes backwards is not a chain of approximations.
        std::vector<std::uint8_t> damaged = output.primaryBlock;
        const std::size_t at = view.LodOffset(view.lodCount - 1U) + 20U;
        const float before = ReadFloat(damaged, at);
        Require(before > 0.0F, "The LOD error fixture did not find an error to lower");
        WriteFloat(damaged, at, -1.0F);
        RenderMeshAssetData refused{};
        Require(!kb::render::bake::ReadBakedMesh(damaged, output.chunks, refused),
            "A LOD chain whose error runs backwards was accepted");
    }
    {
        std::vector<std::uint8_t> damaged = output.primaryBlock;
        WriteFloat(damaged, view.LodOffset(0U) + 20U, 1.0F);
        RenderMeshAssetData refused{};
        Require(!kb::render::bake::ReadBakedMesh(damaged, output.chunks, refused),
            "A base LOD with non-zero approximation error was accepted");
    }
    {
        std::vector<std::uint8_t> damaged = output.primaryBlock;
        WriteFloat(damaged, view.LodOffset(0U) + 24U, 0.5F);
        RenderMeshAssetData refused{};
        Require(!kb::render::bake::ReadBakedMesh(damaged, output.chunks, refused),
            "A camera-dependent screen coverage baked into a LOD was accepted");
    }
    {
        std::vector<std::uint8_t> damaged = output.primaryBlock;
        WriteFloat(damaged, 48U, std::numeric_limits<float>::quiet_NaN());
        RenderMeshAssetData refused{};
        Require(!kb::render::bake::ReadBakedMesh(damaged, output.chunks, refused),
            "An asset bound containing NaN was accepted");
    }
    {
        std::vector<std::uint8_t> damaged = output.primaryBlock;
        WriteFloat(damaged, view.SectionOffset(0U) + 28U, std::numeric_limits<float>::infinity());
        RenderMeshAssetData refused{};
        Require(!kb::render::bake::ReadBakedMesh(damaged, output.chunks, refused),
            "A section bound containing infinity was accepted");
    }
    {
        std::vector<std::uint8_t> damaged = output.primaryBlock;
        WriteFloat(damaged, view.MeshletOffset(0U) + 40U, std::numeric_limits<float>::quiet_NaN());
        RenderMeshAssetData refused{};
        Require(!kb::render::bake::ReadBakedMesh(damaged, output.chunks, refused),
            "A cluster cone containing NaN was accepted");
    }
    {
        std::vector<std::uint8_t> damaged = output.primaryBlock;
        WriteFloat(damaged, view.MeshletOffset(0U) + 52U, 1.0F);
        RenderMeshAssetData refused{};
        Require(!kb::render::bake::ReadBakedMesh(damaged, output.chunks, refused),
            "A finite but geometry-inconsistent cluster cone was accepted");
    }
    {
        std::vector<std::uint8_t> damaged = output.primaryBlock;
        damaged[0] = 'X';
        RenderMeshAssetData refused{};
        Require(!kb::render::bake::ReadBakedMesh(damaged, output.chunks, refused),
            "A payload that is not a baked mesh at all was accepted");
    }
    {
        std::vector<std::uint8_t> damaged = output.primaryBlock;
        damaged.pop_back();
        RenderMeshAssetData refused{};
        Require(!kb::render::bake::ReadBakedMesh(damaged, output.chunks, refused),
            "A truncated primary block was accepted");
    }
    {
        std::vector<std::vector<std::uint8_t>> damaged = output.chunks;
        damaged.pop_back();
        RenderMeshAssetData refused{};
        Require(!kb::render::bake::ReadBakedMesh(output.primaryBlock, damaged, refused),
            "A baked mesh with a chunk missing was accepted");
    }
    {
        std::vector<std::vector<std::uint8_t>> damaged = output.chunks;
        damaged.front().push_back(0U);
        RenderMeshAssetData refused{};
        Require(!kb::render::bake::ReadBakedMesh(output.primaryBlock, damaged, refused),
            "A chunk longer than its declared length was accepted");
    }
    {
        // A valid compressed vertex stream can still decode hostile floats. Re-encode a NaN
        // and update both affected length fields so the reader reaches value validation.
        std::vector<std::vector<std::uint8_t>> damaged = output.chunks;
        std::vector<std::uint8_t>& payload = damaged.front();
        const ChunkRecord chunk = ReadChunk(output.primaryBlock, view, 0U);
        const std::size_t vertexDataOffset = ReadUInt32(payload, 16U);
        const std::size_t oldIndexDataOffset = ReadUInt32(payload, 20U);
        std::vector<std::uint8_t> decoded(
            static_cast<std::size_t>(chunk.vertexCount) * view.vertexStride, 0U);
        Require(meshopt_decodeVertexBuffer(decoded.data(), chunk.vertexCount, view.vertexStride,
                    payload.data() + vertexDataOffset, oldIndexDataOffset - vertexDataOffset) == 0,
            "The non-finite vertex fixture could not decode its stream");
        WriteFloat(decoded, 0U, std::numeric_limits<float>::quiet_NaN());
        std::vector<std::uint8_t> encoded(meshopt_encodeVertexBufferBound(chunk.vertexCount, view.vertexStride), 0U);
        const std::size_t encodedBytes = meshopt_encodeVertexBuffer(
            encoded.data(), encoded.size(), decoded.data(), chunk.vertexCount, view.vertexStride);
        Require(encodedBytes > 0U, "The non-finite vertex fixture could not re-encode its stream");
        encoded.resize(encodedBytes);
        const std::vector<std::uint8_t> encodedIndices(
            payload.begin() + static_cast<std::ptrdiff_t>(oldIndexDataOffset), payload.end());
        payload.resize(vertexDataOffset);
        payload.insert(payload.end(), encoded.begin(), encoded.end());
        WriteUInt32(payload, 20U, static_cast<std::uint32_t>(payload.size()));
        payload.insert(payload.end(), encodedIndices.begin(), encodedIndices.end());
        std::vector<std::uint8_t> primary = output.primaryBlock;
        WriteUInt32(primary, view.ChunkOffset(0U) + 24U, static_cast<std::uint32_t>(payload.size()));
        RenderMeshAssetData refused{};
        Require(!kb::render::bake::ReadBakedMesh(primary, damaged, refused),
            "A codec-valid vertex stream containing NaN was accepted");
    }
    {
        // A chunk-local index that reaches into another meshlet is still invalid: meshlets are
        // culled independently, so one may never fetch a vertex owned by its neighbour. Decode
        // and re-encode the stream so this is a structurally valid codec payload rather than a
        // random compressed-byte mutation.
        std::uint32_t chunkIndex = 0U;
        while (chunkIndex < view.chunkCount && ReadChunk(output.primaryBlock, view, chunkIndex).meshletCount < 2U) {
            ++chunkIndex;
        }
        Require(chunkIndex < view.chunkCount, "The cross-meshlet fixture found no multi-meshlet group");
        const ChunkRecord chunk = ReadChunk(output.primaryBlock, view, chunkIndex);
        std::vector<std::vector<std::uint8_t>> damaged = output.chunks;
        std::vector<std::uint8_t>& payload = damaged[chunkIndex];
        const std::size_t indexDataOffset = ReadUInt32(payload, 20U);
        std::vector<std::uint32_t> decoded(chunk.indexCount, 0U);
        Require(meshopt_decodeIndexBuffer(
                    decoded.data(), decoded.size(), sizeof(std::uint32_t), payload.data() + indexDataOffset,
                    payload.size() - indexDataOffset) == 0,
            "The cross-meshlet fixture could not decode its index stream");
        const std::uint32_t secondMeshletVertexStart = ReadUInt32(payload, kChunkHeaderBytes + kChunkClusterEntryBytes);
        Require(secondMeshletVertexStart > 0U && secondMeshletVertexStart < chunk.vertexCount,
            "The cross-meshlet fixture did not find a neighbouring vertex range");
        decoded.front() = secondMeshletVertexStart;
        std::vector<std::uint8_t> encoded(meshopt_encodeIndexBufferBound(decoded.size(), chunk.vertexCount), 0U);
        const std::size_t encodedBytes =
            meshopt_encodeIndexBuffer(encoded.data(), encoded.size(), decoded.data(), decoded.size());
        Require(encodedBytes > 0U, "The cross-meshlet fixture could not re-encode its index stream");
        encoded.resize(encodedBytes);
        payload.resize(indexDataOffset);
        payload.insert(payload.end(), encoded.begin(), encoded.end());
        std::vector<std::uint8_t> primary = output.primaryBlock;
        WriteUInt32(primary, view.ChunkOffset(chunkIndex) + 24U, static_cast<std::uint32_t>(payload.size()));
        RenderMeshAssetData refused{};
        Require(!kb::render::bake::ReadBakedMesh(primary, damaged, refused),
            "A chunk index that reaches into a neighbouring meshlet was accepted");
    }
    {
        // A chunk that describes its clusters differently from the primary block is a fragment
        // that cannot be trusted to hold whole cluster groups.
        std::vector<std::vector<std::uint8_t>> damaged = output.chunks;
        const std::size_t entry = kChunkHeaderBytes + 4U;
        const std::uint32_t before = ReadUInt32(damaged.front(), entry);
        WriteUInt32(damaged.front(), entry, before + 1U);
        RenderMeshAssetData refused{};
        Require(!kb::render::bake::ReadBakedMesh(output.primaryBlock, damaged, refused),
            "A chunk whose cluster table disagrees with the primary block was accepted");
    }
    {
        std::vector<std::vector<std::uint8_t>> damaged = output.chunks;
        WriteUInt32(damaged.front(), 16U, 0U);
        RenderMeshAssetData refused{};
        Require(!kb::render::bake::ReadBakedMesh(output.primaryBlock, damaged, refused),
            "A chunk whose vertex data starts somewhere else was accepted");
    }
    {
        RenderMeshAssetData noSlots = MakeSphere(16U, 12U, 1.0F);
        noSlots.materialSlots.clear();
        const MeshBakeOutput baked = BakeInto(noSlots, profile);
        Require(baked.status == MeshBakeStatus::Success,
            "A mesh using the implicit default material slot did not bake");
        const BakedMeshView noSlotView = ViewBakedMesh(baked.primaryBlock);
        Require(noSlotView.materialSlotCount == 0U, "The no-slot fixture unexpectedly contains a slot table");
        RenderMeshAssetData control{};
        Require(kb::render::bake::ReadBakedMesh(baked.primaryBlock, baked.chunks, control),
            "A mesh using implicit material slot zero did not read back");
        std::vector<std::uint8_t> damaged = baked.primaryBlock;
        WriteUInt32(damaged, noSlotView.SectionOffset(0U) + 8U, std::numeric_limits<std::uint32_t>::max());
        RenderMeshAssetData refused{};
        Require(!kb::render::bake::ReadBakedMesh(damaged, baked.chunks, refused),
            "A package with no slot table was allowed to request an unbounded material slot allocation");
    }
}

// ---- The package: streaming fragments, and what a package announces about itself ------------

void RunBakedMeshPackFragmentTest() {
    TempStore store{ "kb_mesh_bake_fragments" };
    const RenderMeshAssetData mesh = MakeSphere(32U, 24U, 1.0F);
    const BakeTargetProfile profile = MakeProfile("Mesh.Fragments", BakeIndexWidth::Bits32, 32768U);

    const std::filesystem::path packPath = store.Root() / "sphere.kbpack";
    MeshBakeOutput output{};
    {
        AssetPackWriter writer{ packPath, profile };
        output = BakeMesh(mesh, profile, writer);
        Require(output.status == MeshBakeStatus::Success, "A sphere did not bake into a package");
        Require(writer.Finish() == BakedAssetSinkStatus::Success, "A package holding a baked mesh did not publish");
    }
    const BakedMeshView view = ViewBakedMesh(output.primaryBlock);
    Require(view.chunkCount > 1U, "The fragment fixture produced a single chunk, so its rules cannot be reached");

    AssetPackReader reader;
    Require(reader.Mount(packPath) == AssetPackReadStatus::Success, "A package holding a baked mesh did not mount");
    Require(reader.Header().fragmentCount == view.chunkCount,
        "The package's fragment count is not the number of geometry chunks in it");
    Require(reader.Header().fragmentIndexOffset == 256U + reader.Header().indexBytes,
        "The fragment index is not where the format puts it");
    Require(reader.Header().fragmentIndexBytes == static_cast<std::uint64_t>(reader.Header().fragmentCount) * 48U,
        "The fragment index is not the length its count implies");
    Require(reader.Header().fragmentAlignmentBytes == profile.packageBlockAlignmentBytes,
        "The fragment alignment is not the alignment the fragments were placed at");
    Require(reader.Fragments().size() == view.chunkCount, "The reader did not hand back every fragment");

    Require(reader.Artifacts().size() == 1U, "A package holding one mesh has more than one artifact");
    const kb::assets::bake::AssetPackArtifactEntry& artifact = reader.Artifacts().front();
    Require(artifact.assetTypeId == "StaticMesh", "A baked mesh in a package does not announce itself as a mesh");

    const SplicedGeometry geometry = SpliceChunks(output, view);
    for (std::uint32_t chunkIndex = 0U; chunkIndex < view.chunkCount; ++chunkIndex) {
        const ChunkRecord chunk = ReadChunk(output.primaryBlock, view, chunkIndex);
        const kb::assets::bake::AssetPackFragmentEntry& fragment = reader.Fragments()[chunkIndex];
        Require(fragment.clusterCount == chunk.meshletCount,
            "A fragment declares a different number of clusters than its group holds");
        Require(fragment.bytes == chunk.byteLength, "A fragment is not the length of the chunk it covers");
        Require(fragment.offset % reader.Header().fragmentAlignmentBytes == 0U,
            "A fragment does not start on the alignment the header claims for it");
        // A fragment is exactly one block, so the geometry it names is geometry the artifact
        // index describes too -- never a range only the fragment index knows about.
        const std::string blockName = "geom" + std::to_string(chunkIndex);
        const auto block = std::ranges::find_if(
            artifact.blocks, [&blockName](const kb::assets::bake::AssetPackBlockEntry& candidate) {
                return candidate.name == blockName;
            });
        Require(block != artifact.blocks.end(), "A geometry chunk is not in the package's artifact index");
        Require(block->offset == fragment.offset && block->storedBytes == fragment.bytes,
            "A fragment does not coincide with the block it is supposed to be");
        Require(block->residency == kb::assets::bake::BakedAssetBlockResidency::Streaming,
            "A geometry chunk was not stored as a streaming block");
        for (std::uint32_t vertex = 0U; vertex < chunk.vertexCount; ++vertex) {
            const std::array<float, 3>& position = geometry.positions[chunk.vertexStart + vertex];
            for (std::size_t axis = 0U; axis < 3U; ++axis) {
                Require(position[axis] >= fragment.boundsMin[axis] && position[axis] <= fragment.boundsMax[axis],
                    "A fragment's box does not contain a vertex of the chunk it covers");
            }
        }
    }

    // A package the reader believes has to be a package a reader can read: every block the
    // artifact names comes back, and the mesh they compose is the mesh that was baked.
    std::vector<std::uint8_t> primary;
    Require(reader.ReadBlock(artifact, "primary", primary) == AssetPackReadStatus::Success,
        "The primary block of a packaged mesh did not read back");
    Require(primary == output.primaryBlock, "The primary block in the package is not the one the baker produced");
}

void RunBakedMeshLoaderRequiresCanonicalFragmentsTest() {
    TempStore store{ "kb_mesh_bake_loader_fragments" };
    const BakeTargetProfile profile = MakeProfile("Mesh.LoaderFragments", BakeIndexWidth::Bits32, 32768U);
    RecordingSink recorded;
    Require(BakeMesh(MakeSphere(32U, 24U, 1.0F), profile, recorded).status == MeshBakeStatus::Success &&
            recorded.auxiliary.size() > 1U,
        "The loader-fragment fixture did not produce several geometry chunks");

    const std::filesystem::path omitted = store.Root() / "omitted.kbpack";
    Require(WriteReplayedMeshPack(omitted, profile, recorded, ReplayedFragmentMode::Omitted),
        "The test could not publish a structurally legal mesh pack without fragments");
    Require(!MeshPackLoads(omitted),
        "The mesh loader accepted geometry chunks with no streaming fragment catalogue");

    const std::filesystem::path widened = store.Root() / "widened.kbpack";
    Require(WriteReplayedMeshPack(widened, profile, recorded, ReplayedFragmentMode::WidenedBounds),
        "The test could not publish a mesh pack with self-consistent but false fragment bounds");
    Require(!MeshPackLoads(widened),
        "The mesh loader accepted fragment bounds that were not derived from their chunk geometry");

    const std::filesystem::path wrongCount = store.Root() / "wrong-count.kbpack";
    Require(WriteReplayedMeshPack(wrongCount, profile, recorded, ReplayedFragmentMode::WrongClusterCount),
        "The test could not publish a mesh pack with a false fragment cluster count");
    Require(!MeshPackLoads(wrongCount),
        "The mesh loader accepted a fragment cluster count that disagreed with its chunk");
}

void RunBakedMeshPackRefusesBrokenFragmentIndexTest() {
    TempStore store{ "kb_mesh_bake_hostile_fragments" };
    const RenderMeshAssetData mesh = MakeSphere(32U, 24U, 1.0F);
    const BakeTargetProfile profile = MakeProfile("Mesh.Hostile", BakeIndexWidth::Bits32, 32768U);
    const std::filesystem::path packPath = store.Root() / "sphere.kbpack";
    {
        AssetPackWriter writer{ packPath, profile };
        Require(BakeMesh(mesh, profile, writer).status == MeshBakeStatus::Success, "A sphere did not bake");
        Require(writer.Finish() == BakedAssetSinkStatus::Success, "A package did not publish");
    }
    std::vector<std::uint8_t> original;
    {
        std::ifstream input{ packPath, std::ios::binary };
        Require(input.is_open(), "A published package could not be reopened");
        original.assign(std::istreambuf_iterator<char>{ input }, std::istreambuf_iterator<char>{});
    }
    Require(!original.empty(), "A published package is empty");

    AssetPackReader control;
    Require(control.Mount(packPath) == AssetPackReadStatus::Success, "The hostile-package fixture does not mount clean");
    const std::uint64_t fragmentIndexOffset = control.Header().fragmentIndexOffset;
    const std::uint32_t fragmentCount = control.Header().fragmentCount;
    const std::uint64_t firstFragmentOffset = control.Fragments().front().offset;
    const std::uint64_t firstFragmentBytes = control.Fragments().front().bytes;
    control.Unmount();

    // The header offsets restated here, so a fixture that patches the wrong word is caught by
    // the assertion that the field held what it was supposed to hold.
    constexpr std::size_t kFragmentIndexOffsetField = 72U;
    constexpr std::size_t kFragmentIndexBytesField = 80U;
    constexpr std::size_t kFragmentCountField = 88U;
    constexpr std::size_t kFragmentAlignmentField = 92U;

    struct Fixture {
        const char* name;
        std::size_t offset;
        std::uint64_t expectedBefore;
        std::uint64_t after;
        std::size_t width;
    };
    std::vector<Fixture> fixtures;
    fixtures.push_back(Fixture{ "a fragment index somewhere else", kFragmentIndexOffsetField, fragmentIndexOffset,
        fragmentIndexOffset + 256U, 8U });
    fixtures.push_back(Fixture{ "a fragment index of the wrong length", kFragmentIndexBytesField,
        static_cast<std::uint64_t>(fragmentCount) * 48U, static_cast<std::uint64_t>(fragmentCount) * 48U + 48U, 8U });
    fixtures.push_back(Fixture{ "a fragment count with no bytes behind it", kFragmentCountField, fragmentCount,
        fragmentCount + 1U, 4U });
    fixtures.push_back(Fixture{ "a fragment alignment finer than the package's", kFragmentAlignmentField,
        profile.packageBlockAlignmentBytes, 128U, 4U });
    fixtures.push_back(Fixture{ "a fragment that is only half a block", fragmentIndexOffset + 8U, firstFragmentBytes,
        firstFragmentBytes / 2U, 8U });
    fixtures.push_back(Fixture{ "a fragment that starts inside a block", fragmentIndexOffset + 0U,
        firstFragmentOffset, firstFragmentOffset + 256U, 8U });
    fixtures.push_back(Fixture{ "no fragments at all, with a fragment index still behind the header",
        kFragmentCountField, fragmentCount, 0U, 4U });
    fixtures.push_back(Fixture{ "a fragment holding no cluster groups", fragmentIndexOffset + 16U,
        control.Fragments().empty() ? 0U : 1U, 0U, 4U });

    for (const Fixture& fixture : fixtures) {
        std::vector<std::uint8_t> bytes = original;
        std::uint64_t before = 0U;
        for (std::size_t index = 0U; index < fixture.width; ++index) {
            before |= static_cast<std::uint64_t>(bytes[fixture.offset + index]) << (index * 8U);
        }
        if (fixture.offset == fragmentIndexOffset + 16U) {
            // The cluster count is whatever the first fragment holds; only "it is not zero"
            // matters, so the fixture reads it rather than predicting it.
            Require(before != 0U, "A fragment fixture found no cluster count where it expected one");
        } else {
            Require(before == fixture.expectedBefore,
                "A fragment fixture did not find the field it names at the offset it patches");
        }
        for (std::size_t index = 0U; index < fixture.width; ++index) {
            bytes[fixture.offset + index] = static_cast<std::uint8_t>((fixture.after >> (index * 8U)) & 0xFFU);
        }
        const std::filesystem::path damagedPath = store.Root() / "damaged.kbpack";
        {
            std::ofstream output{ damagedPath, std::ios::binary | std::ios::trunc };
            Require(output.is_open(), "A hostile package fixture could not be written");
            output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        }
        AssetPackReader hostile;
        if (hostile.Mount(damagedPath) == AssetPackReadStatus::Success) {
            std::fprintf(stderr, "the fragment damage that was accepted: %s\n", fixture.name);
        }
        Require(hostile.Mount(damagedPath) != AssetPackReadStatus::Success,
            "A package whose fragment index does not describe its own blocks was mounted anyway");
    }

    {
        // Widening a fragment bound keeps the catalogue structurally valid. It must still be
        // detected because the checksum authenticates the fragment catalogue as well as the
        // artifact catalogue.
        std::vector<std::uint8_t> bytes = original;
        const std::size_t boundOffset = static_cast<std::size_t>(fragmentIndexOffset) + 24U;
        const float before = ReadFloat(bytes, boundOffset);
        Require(std::isfinite(before), "The checksum fixture found no finite fragment bound");
        WriteFloat(bytes, boundOffset, before - 1.0F);
        const std::filesystem::path checksumPath = store.Root() / "checksum.kbpack";
        {
            std::ofstream output{ checksumPath, std::ios::binary | std::ios::trunc };
            output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        }
        AssetPackReader checksum;
        Require(checksum.Mount(checksumPath) == AssetPackReadStatus::IndexCorrupt,
            "A structurally valid fragment-catalogue mutation was not caught by the catalogue checksum");
    }

    {
        // A fragment index that is valid in every respect except WHERE it is: an exact copy of
        // it appended past the last block, with the header pointed at the copy and its own
        // recorded length grown to match. Every entry still names a real block, so nothing but
        // the rule that fixes the fragment index immediately behind the artifact index can
        // refuse this -- which is what makes that rule falsifiable rather than a belt over a
        // check somewhere else.
        std::vector<std::uint8_t> bytes = original;
        const std::size_t fragmentIndexBytes = static_cast<std::size_t>(fragmentCount) * 48U;
        const std::uint64_t appendedOffset = bytes.size();
        bytes.insert(
            bytes.end(),
            original.begin() + static_cast<std::ptrdiff_t>(fragmentIndexOffset),
            original.begin() + static_cast<std::ptrdiff_t>(fragmentIndexOffset) +
                static_cast<std::ptrdiff_t>(fragmentIndexBytes));
        for (std::size_t index = 0U; index < 8U; ++index) {
            bytes[kFragmentIndexOffsetField + index] = static_cast<std::uint8_t>((appendedOffset >> (index * 8U)) & 0xFFU);
            bytes[64U + index] = static_cast<std::uint8_t>((bytes.size() >> (index * 8U)) & 0xFFU);
        }
        const std::filesystem::path movedPath = store.Root() / "moved.kbpack";
        {
            std::ofstream output{ movedPath, std::ios::binary | std::ios::trunc };
            output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        }
        AssetPackReader moved;
        Require(moved.Mount(movedPath) != AssetPackReadStatus::Success,
            "A package whose fragment index sits somewhere the format does not put it was mounted anyway");
    }

    // ...and the untouched bytes, written the same way, must still mount, so the refusals above
    // belong to the damage rather than to the copying.
    const std::filesystem::path copyPath = store.Root() / "copy.kbpack";
    {
        std::ofstream output{ copyPath, std::ios::binary | std::ios::trunc };
        output.write(reinterpret_cast<const char*>(original.data()), static_cast<std::streamsize>(original.size()));
    }
    AssetPackReader copy;
    Require(copy.Mount(copyPath) == AssetPackReadStatus::Success,
        "A byte-for-byte copy of a good package did not mount, so the fixtures prove nothing");
}

void RunBakedMeshFragmentDeclarationTest() {
    // The fragment declaration is the only thing a baker tells a container about where a
    // cluster group ends, so a declaration that says nothing usable has to be a refusal rather
    // than a fragment index nobody can prioritise against. Both sinks answer, because an
    // artifact has to be describable through either of them.
    TempStore store{ "kb_mesh_bake_fragment_rules" };
    const BakeTargetProfile profile = kb::assets::bake::WindowsX64BakeTargetProfile();

    struct Case {
        const char* name;
        kb::assets::bake::BakedAssetBlockFragment fragment;
        bool legal;
    };
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float infinity = std::numeric_limits<float>::infinity();
    const std::vector<Case> cases{
        Case{ "a box around one cluster group", { { -1.0F, -1.0F, -1.0F }, { 1.0F, 1.0F, 1.0F }, 1U }, true },
        Case{ "a box of no width at all", { { 1.0F, 1.0F, 1.0F }, { 1.0F, 1.0F, 1.0F }, 3U }, true },
        Case{ "no cluster groups", { { -1.0F, -1.0F, -1.0F }, { 1.0F, 1.0F, 1.0F }, 0U }, false },
        Case{ "an inside-out box", { { 1.0F, -1.0F, -1.0F }, { -1.0F, 1.0F, 1.0F }, 2U }, false },
        Case{ "a box corner that is not a number", { { nan, -1.0F, -1.0F }, { 1.0F, 1.0F, 1.0F }, 2U }, false },
        Case{ "an infinite box", { { -infinity, -1.0F, -1.0F }, { 1.0F, 1.0F, 1.0F }, 2U }, false },
    };

    const std::vector<std::uint8_t> payload(64U, 7U);
    for (const Case& testCase : cases) {
        Require(kb::assets::bake::IsValidBakedAssetBlockFragment(testCase.fragment) == testCase.legal,
            "A fragment declaration was judged differently from the rule this suite states");

        BakedAssetBlock block{};
        block.name = "geom0";
        block.residency = kb::assets::bake::BakedAssetBlockResidency::Streaming;
        block.alignmentBytes = profile.packageBlockAlignmentBytes;
        block.fragment = testCase.fragment;

        kb::assets::bake::AssetBakeKey key{};
        key.sourceContentHash = 1U;
        key.bakerId = "Mesh";
        key.bakerVersion = "1";
        key.targetProfileId = std::string{ profile.identifier };
        key.targetProfileHash = kb::assets::bake::BakeTargetProfileFingerprint(profile);
        const BakedAssetDescriptor descriptor{ .key = key, .assetTypeId = "StaticMesh" };

        const BakedAssetSinkStatus expected =
            testCase.legal ? BakedAssetSinkStatus::Success : BakedAssetSinkStatus::InvalidFragment;
        {
            AssetPackWriter writer{ store.Root() / "rules.kbpack", profile };
            Require(writer.BeginAsset(descriptor) == BakedAssetSinkStatus::Success,
                "The fragment rule test could not open an artifact on a package writer");
            Require(writer.WritePrimaryBlock(payload, profile.packageBlockAlignmentBytes) ==
                    BakedAssetSinkStatus::Success,
                "The fragment rule test could not write a primary block");
            const BakedAssetSinkStatus status = writer.WriteAuxiliaryBlock(block, payload);
            if (status != expected) {
                std::fprintf(stderr, "the fragment a package writer answered wrongly: %s (%s)\n",
                    testCase.name, std::string{ kb::assets::bake::ToString(status) }.c_str());
            }
            Require(status == expected,
                "A package writer did not answer a fragment declaration the way the rule says");
            writer.AbortAsset();
        }
        {
            kb::assets::bake::LooseBakedAssetSink loose{ store.Root() / "loose" };
            Require(loose.BeginAsset(descriptor) == BakedAssetSinkStatus::Success,
                "The fragment rule test could not open an artifact on a loose sink");
            Require(loose.WritePrimaryBlock(payload, profile.packageBlockAlignmentBytes) ==
                    BakedAssetSinkStatus::Success,
                "The fragment rule test could not write a primary block to a loose sink");
            const BakedAssetSinkStatus status = loose.WriteAuxiliaryBlock(block, payload);
            if (status != expected) {
                std::fprintf(stderr, "the fragment a loose sink answered wrongly: %s (%s)\n",
                    testCase.name, std::string{ kb::assets::bake::ToString(status) }.c_str());
            }
            Require(status == expected, "A loose sink did not answer a fragment declaration the way the rule says");
            loose.AbortAsset();
        }
    }

    kb::assets::bake::AssetBakeKey key{};
    key.sourceContentHash = 2U;
    key.bakerId = "Mesh";
    key.bakerVersion = "1";
    key.targetProfileId = std::string{ profile.identifier };
    key.targetProfileHash = kb::assets::bake::BakeTargetProfileFingerprint(profile);
    const BakedAssetDescriptor descriptor{ .key = key, .assetTypeId = "StaticMesh" };
    BakedAssetBlock residentFragment{};
    residentFragment.name = "geom0";
    residentFragment.residency = kb::assets::bake::BakedAssetBlockResidency::Resident;
    residentFragment.alignmentBytes = profile.packageBlockAlignmentBytes;
    residentFragment.fragment = kb::assets::bake::BakedAssetBlockFragment{
        .boundsMin = { -1.0F, -1.0F, -1.0F },
        .boundsMax = { 1.0F, 1.0F, 1.0F },
        .clusterCount = 1U,
    };
    {
        AssetPackWriter writer{ store.Root() / "resident-fragment.kbpack", profile };
        Require(writer.BeginAsset(descriptor) == BakedAssetSinkStatus::Success &&
                writer.WritePrimaryBlock(payload, profile.packageBlockAlignmentBytes) ==
                    BakedAssetSinkStatus::Success,
            "The residency fixture could not open its package artifact");
        Require(writer.WriteAuxiliaryBlock(residentFragment, payload) == BakedAssetSinkStatus::InvalidFragment,
            "A resident block was allowed to advertise itself as a streaming fragment");
        writer.AbortAsset();
    }
    {
        kb::assets::bake::LooseBakedAssetSink loose{ store.Root() / "resident-loose" };
        Require(loose.BeginAsset(descriptor) == BakedAssetSinkStatus::Success &&
                loose.WritePrimaryBlock(payload, profile.packageBlockAlignmentBytes) ==
                    BakedAssetSinkStatus::Success,
            "The residency fixture could not open its loose artifact");
        Require(loose.WriteAuxiliaryBlock(residentFragment, payload) == BakedAssetSinkStatus::InvalidFragment,
            "The loose sink accepted a resident block as a streaming fragment");
        loose.AbortAsset();
    }
}

void RunBakedMeshPackDiscoveryTest() {
    TempStore store{ "kb_mesh_bake_discovery" };
    const BakeTargetProfile profile = kb::assets::bake::WindowsX64BakeTargetProfile();

    const std::filesystem::path meshPack = store.Root() / "sphere.kbpack";
    {
        AssetPackWriter writer{ meshPack, profile };
        Require(BakeMesh(MakeSphere(16U, 12U, 1.0F), profile, writer).status == MeshBakeStatus::Success,
            "A sphere did not bake into a package");
        Require(writer.Finish() == BakedAssetSinkStatus::Success, "A mesh package did not publish");
    }

    // A texture package beside it, so the answer cannot come from "there is only one kind of
    // package in this folder".
    const std::filesystem::path texturePack = store.Root() / "flat.kbpack";
    {
        std::vector<std::uint8_t> source(4U * 4U * 4U, 0U);
        for (std::size_t texel = 0U; texel < 16U; ++texel) {
            source[texel * 4U + 0U] = 200U;
            source[texel * 4U + 1U] = 120U;
            source[texel * 4U + 2U] = 60U;
            source[texel * 4U + 3U] = 255U;
        }
        std::vector<std::uint8_t> tga;
        tga.assign(18U, 0U);
        tga[2] = 2U;
        tga[12] = 4U;
        tga[14] = 4U;
        tga[16] = 32U;
        tga[17] = 0x20U;
        for (std::size_t texel = 0U; texel < 16U; ++texel) {
            tga.push_back(source[texel * 4U + 2U]);
            tga.push_back(source[texel * 4U + 1U]);
            tga.push_back(source[texel * 4U + 0U]);
            tga.push_back(source[texel * 4U + 3U]);
        }
        AssetPackWriter writer{ texturePack, profile };
        const kb::render::bake::TextureBakeOutput baked = kb::render::bake::BakeTextureBytes(
            tga,
            kb::render::bake::TextureBakeSettings{},
            profile,
            TextureCompressionFamily::BlockCompressedBaseline,
            writer);
        Require(baked.status == kb::render::bake::TextureBakeStatus::Success,
            "The discovery test could not bake its texture package");
        Require(writer.Finish() == BakedAssetSinkStatus::Success, "A texture package did not publish");
    }

    // A path denotes one runtime asset. With no digest or subasset selector in that path, two
    // mesh artifacts are ambiguous and discovery must not register either one by guessing.
    const std::filesystem::path ambiguousPack = store.Root() / "ambiguous.kbpack";
    {
        AssetPackWriter writer{ ambiguousPack, profile };
        Require(BakeMesh(MakeSphere(16U, 12U, 1.0F), profile, writer).status == MeshBakeStatus::Success &&
                BakeMesh(MakeSphere(16U, 12U, 2.0F), profile, writer).status == MeshBakeStatus::Success,
            "Two mesh artifacts did not bake into the ambiguity fixture");
        Require(writer.Finish() == BakedAssetSinkStatus::Success, "An ambiguous mesh package did not publish");
    }

    // Bytes that are not a package at all, under the package extension.
    {
        std::ofstream junk{ store.Root() / "junk.kbpack", std::ios::binary | std::ios::trunc };
        const std::string prose = "this is not a package, whatever its name says";
        junk.write(prose.data(), static_cast<std::streamsize>(prose.size()));
    }

    kb::scene::Scene scene;
    kb::assets::AssetManager& manager = scene.Assets().Manager();
    // The texture loader first, deliberately: it is the one that would win a lookup by
    // extension, so a mesh package typed as a texture would mean discovery guessed.
    Require(manager.RegisterLoader(std::make_unique<RenderTextureAssetLoader>()) &&
            manager.RegisterLoader(std::make_unique<RenderMeshAssetLoader>()),
        "The discovery test could not register its loaders");
    Require(manager.Mounts().Mount("Game", store.Root()), "The discovery test could not mount its root");
    static_cast<void>(manager.DiscoverMountedAssets());

    const kb::assets::AssetMetadata* meshMetadata = manager.Registry().FindByPath("/Game/sphere.kbpack");
    const kb::assets::AssetMetadata* textureMetadata = manager.Registry().FindByPath("/Game/flat.kbpack");
    Require(meshMetadata != nullptr && textureMetadata != nullptr, "A published package was not discovered at all");
    Require(meshMetadata->type == "RenderMesh",
        "A package holding a baked mesh was discovered as something other than a mesh");
    Require(textureMetadata->type == "RenderTexture",
        "A package holding a baked texture was no longer discovered as a texture");
    Require(manager.Registry().FindByPath("/Game/junk.kbpack") == nullptr,
        "A file that only looks like a package was discovered as an asset anyway");
    Require(manager.Registry().FindByPath("/Game/ambiguous.kbpack") == nullptr,
        "Discovery picked one of two artifacts in a package without a subasset selector");

    const kb::assets::AssetHandle<RenderMeshAssetData> loaded =
        manager.Load<RenderMeshAssetData>(meshMetadata->id);
    Require(loaded.IsLoaded(), "A discovered mesh package did not load");
    Require(!loaded->meshlets.empty() && !loaded->lods.empty() && !loaded->indices32.empty(),
        "A mesh loaded from a package carries no clusters");
    Require(loaded->materialSlots.size() == 1U && loaded->materialSlots.front().defaultMaterialAssetId == 7U,
        "A mesh loaded from a package lost its material slot");

    kb::scene::Scene conflictingScene;
    kb::assets::AssetManager& conflictingManager = conflictingScene.Assets().Manager();
    Require(conflictingManager.RegisterLoader(std::make_unique<RenderMeshAssetLoader>()) &&
            conflictingManager.RegisterLoader(std::make_unique<ConflictingBakedMeshLoader>()),
        "The ownership-ambiguity test could not register two distinct loader types");
    Require(conflictingManager.Mounts().Mount("Game", store.Root()),
        "The ownership-ambiguity test could not mount its package root");
    static_cast<void>(conflictingManager.DiscoverMountedAssets());
    Require(conflictingManager.Registry().FindByPath("/Game/sphere.kbpack") == nullptr,
        "Discovery chose the first of two loaders claiming the same baked artifact type");
}

void RunMeshBakeTests() {
    RunBakedMeshFormatSelfCheckTest();
    RunBakedMeshClusterLimitsTest();
    RunBakedMeshClusterBoundsAndConeTest();
    RunBakedMeshLodChainTest();
    RunBakedMeshLodErrorIsAbsoluteTest();
    RunBakedMeshSimplificationKeepsAttributesTest();
    RunBakedMeshMaterialSeamTest();
    RunBakedMeshMaterialMetadataTest();
    RunBakedMeshIndexWidthTest();
    RunSectionLocalTangentGenerationTest();
    RunSectionLocalIndexCompactionTest();
    RunSectionLocalBoundsRefreshTest();
    RunBakedMeshChunkBudgetTest();
    RunBakedMeshDeterminismTest();
    RunBakedMeshKeyTest();
    RunBakedMeshSinkContractTest();
    RunBakedMeshDegenerateSourceTest();
    RunBakedMeshSmallestMeshesTest();
    RunBakedMeshReaderCapsResidentGeometryTest();
    RunBakedMeshReaderRefusesTamperedPayloadTest();
    RunBakedMeshPackFragmentTest();
    RunBakedMeshLoaderRequiresCanonicalFragmentsTest();
    RunBakedMeshPackRefusesBrokenFragmentIndexTest();
    RunBakedMeshFragmentDeclarationTest();
    RunBakedMeshPackDiscoveryTest();
    std::puts("mesh bake tests passed");
}

} // namespace kb::render::tests
