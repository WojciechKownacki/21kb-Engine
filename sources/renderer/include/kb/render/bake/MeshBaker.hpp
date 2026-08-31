#pragma once

#include "engine/assets/bake/AssetBakeKey.hpp"
#include "engine/assets/bake/BakeTargetProfile.hpp"
#include "engine/assets/bake/BakedAssetSink.hpp"
#include "kb/render/resources/RenderMeshAssetBuilder.hpp"

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

// Turns an imported mesh into the exact bytes a cluster renderer streams: real clusters with
// real bounds and cones, a LOD chain whose error is absolute, indices at the width the target
// guarantees, and geometry cut into chunks that fit the target's budget.
//
// It imports nothing. RenderMeshAssetBuilder::LoadObj/LoadGltf/LoadFbx already read the source
// formats, so this baker starts from a RenderMeshAssetData and never opens a file. It lives in
// kb_renderer for the same reason the texture baker does: kb_engine holds the bake seam and
// links neither bgfx nor the geometry library the clusters come from.
//
// The clusters come from meshoptimizer, which the repository already vendors in full
// (third_party/bgfx.cmake/bgfx/3rdparty/meshoptimizer). Nothing here reimplements a clusterizer
// or a simplifier.
namespace kb::render::bake {

// Identity of this baker inside a bake key. `bakerId` scopes `bakerVersion`, so bumping the
// version below re-bakes every mesh and leaves every other baker's cache untouched.
inline constexpr std::string_view kMeshBakerId = "Mesh";
inline constexpr std::string_view kMeshBakerVersion = "3";

// Runtime type of the artifact this baker publishes; a path component of the bake store, and
// what a package announces about itself so discovery can ask instead of guessing from .kbpack.
inline constexpr std::string_view kMeshBakedAssetTypeId = "StaticMesh";

// Magic and version of the baked mesh payload itself, in the primary block.
inline constexpr std::string_view kBakedMeshMagic = "21KBMESH";
inline constexpr std::uint32_t kBakedMeshFormatVersion = 3U;

// Cluster shape. These are the algorithm's parameters, not user settings: they are folded into
// the bake key's settings hash, so editing one of them re-bakes every mesh by itself, without
// depending on anyone remembering to bump kMeshBakerVersion as well.
//
// 64 vertices and 124 triangles are the sizes a cluster has to have to be dispatchable: 64 is
// the vertex count every mesh-shader-class API guarantees per group, and 124 leaves the
// primitive count under 128 with room for the header a payload carries. The minimum exists
// because meshopt_buildMeshletsFlex splits a cluster whose bounds grew past `splitFactor` times
// the expected size, and without a floor that split runs down to slivers whose cone tells a
// culler nothing.
inline constexpr std::uint32_t kMaxClusterVertices = 64U;
inline constexpr std::uint32_t kMinClusterTriangles = 32U;
inline constexpr std::uint32_t kMaxClusterTriangles = 124U;
// How much cluster compactness is traded for cone tightness, and how far a cluster's bounds may
// grow before it is split. A cone weight of zero produces clusters that pack well and cull by
// normal not at all, which is the failure the current placeholder cone already has.
inline constexpr float kClusterConeWeight = 0.25F;
inline constexpr float kClusterSplitFactor = 2.0F;
// One streaming fragment is one topology-aware cluster group. meshoptimizer may grow a
// partition up to target + target/3; the baker lowers this target for unusually small profile
// budgets, but never raises it above this locality target.
inline constexpr std::uint32_t kTargetClusterGroupSize = 8U;

// LOD chain. Each level is simplified from the level above it, halving the triangle count while
// the error stays inside an ABSOLUTE budget -- a fraction of the mesh's own extent, measured by
// meshopt_simplifyScale. Nothing here reads a viewport, a resolution or a field of view, and
// nothing may: an error baked against a camera has to be re-baked when the camera changes.
inline constexpr std::uint32_t kMaxLodCount = 4U;
inline constexpr float kLodTriangleRatio = 0.5F;
inline constexpr float kLodErrorFractionOfScale = 0.02F;

// Relative priority of every non-position attribute in the simplifier's error metric. Omitting
// any channel lets a collapse preserve the silhouette while erasing a tangent seam, lightmap
// UV, vertex colour/alpha, normal or primary UV detail.
inline constexpr float kSimplifyNormalWeight = 1.0F;
inline constexpr float kSimplifyTangentWeight = 1.0F;
inline constexpr float kSimplifyUvWeight = 0.5F;
inline constexpr float kSimplifyColorWeight = 0.25F;

enum class MeshBakeStatus : std::uint8_t {
    Success,
    // The profile itself is not bakeable (IsValidBakeTargetProfile said no).
    InvalidProfile,
    // No vertices, no indices, or no sections. There is nothing to bake and a zero-cluster
    // artifact would be a mesh that loads and draws nothing.
    EmptySource,
    // The index buffer is not whole triangles, or an index does not name a vertex.
    MalformedIndices,
    // A section's range is empty or runs past the index buffer, or two sections describe the
    // same indices.
    MalformedSections,
    // Material names or embedded materials do not line up with the slot table, contain a
    // non-finite/unknown value, carry a transient runtime handle, or exceed the bounded
    // metadata representation of the baked payload.
    MalformedMaterials,
    // A vertex float is not finite, or finite source values overflow a derived bound or the
    // simplifier metric. NaN/Inf would propagate into culling and LOD data instead of failing.
    NonFiniteGeometry,
    // A cluster came out with no extent, i.e. its triangles are all one point. Its sphere and
    // its cone would say nothing, and RenderMeshletDesc::IsValid rejects it.
    DegenerateGeometry,
    // The source already carries authored LODs or terrain layer passes. Re-clustering those
    // would either drop the authoring or invent a level ordering nobody asked for.
    UnsupportedSourceShape,
    // meshoptimizer returned no clusters for a section it was given triangles for.
    ClusterBuildFailed,
    // meshoptimizer returned malformed topology or a non-finite error for an LOD step.
    SimplificationFailed,
    // One indivisible cluster group exceeds the target's local index address space.
    IndexWidthExceeded,
    // The current runtime materializes all decoded chunks as one mesh resource. Until it owns
    // fragment-resident buffers independently, a bake larger than that resident budget would
    // publish an artifact its own loader must refuse.
    ResidentBudgetExceeded,
    // The vendored geometry codec refused a buffer despite the baker providing its documented
    // worst-case capacity. No raw fallback is published under a format that promises encoding.
    EncodeFailed,
    // The profile's geometry chunk budget cannot hold even a single cluster, so no split
    // produces a legal chunk.
    ChunkBudgetTooSmall,
    // The sink refused the artifact; the sink's own status says why.
    SinkRejected,
};

[[nodiscard]] std::string_view ToString(MeshBakeStatus status) noexcept;

struct MeshBakeOutput {
    MeshBakeStatus status = MeshBakeStatus::EmptySource;
    // Set for every call that got as far as reading the source, including failures - a caller
    // deciding whether to skip an unchanged bake needs the key before the answer.
    kb::assets::bake::AssetBakeKey key{};
    // The header, the LOD/section/cluster tables and the chunk table: the artifact's primary
    // block. Returned as well as written so a caller can verify a bake without going back to
    // the store.
    std::vector<std::uint8_t> primaryBlock;
    // The geometry, one entry per streaming fragment, in the order the chunk table lists them.
    std::vector<std::vector<std::uint8_t>> chunks;
    std::uint32_t lodCount = 0U;
    std::uint32_t sectionCount = 0U;
    std::uint32_t clusterCount = 0U;
    // The status the sink returned, when the failure was the sink's.
    kb::assets::bake::BakedAssetSinkStatus sinkStatus = kb::assets::bake::BakedAssetSinkStatus::Success;
};

// Name of the auxiliary block one geometry chunk is written under. Both the baker and the
// loader build it here, so a renamed chunk is a compile error rather than a missing block.
[[nodiscard]] std::string BakedMeshChunkBlockName(std::uint32_t chunkIndex);

// The key for this bake. Everything that changes the output bytes is in it: the source
// geometry, the target profile and its fingerprint, this baker's id and version, and the
// cluster and LOD parameters above.
[[nodiscard]] kb::assets::bake::AssetBakeKey MakeMeshBakeKey(
    const RenderMeshAssetData& source,
    const kb::assets::bake::BakeTargetProfile& profile);

// Bakes `source` for one target and hands the result to `sink`: the primary block, then one
// auxiliary block per encoded geometry chunk, each declared as a streaming fragment. Deterministic:
// the same source and the same profile produce byte-identical output on every run.
[[nodiscard]] MeshBakeOutput BakeMesh(
    const RenderMeshAssetData& source,
    const kb::assets::bake::BakeTargetProfile& profile,
    kb::assets::bake::IBakedAssetSink& sink);

// Reads a primary block and its chunks back into the runtime's mesh shape. The counterpart of
// the bake, and the only way a baked mesh becomes a RenderMeshAssetData - the ordinary importer
// path is untouched. Returns false for anything this baker could not have produced, and treats
// the bytes as hostile: every table entry is checked against the counts the header declares and
// every chunk against the range the chunk table gives it, before a byte of geometry is used.
[[nodiscard]] bool ReadBakedMesh(
    std::span<const std::uint8_t> primaryBlock,
    std::span<const std::vector<std::uint8_t>> chunks,
    RenderMeshAssetData& out);

// Verifies the target-dependent promises that are intentionally not part of generic payload
// decoding: index width and the maximum encoded streaming-fragment size. Call after (or beside)
// ReadBakedMesh when accepting a package for a concrete target profile.
[[nodiscard]] bool BakedMeshMatchesTargetProfile(
    std::span<const std::uint8_t> primaryBlock,
    std::span<const std::vector<std::uint8_t>> chunks,
    const kb::assets::bake::BakeTargetProfile& profile) noexcept;

} // namespace kb::render::bake
