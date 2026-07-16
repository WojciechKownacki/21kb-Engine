#include "kb/render/resources/BuiltInParticleQuadMesh.hpp"

#include "kb/render/resources/RenderMeshAssetBuilder.hpp"

namespace kb::render {
namespace {

void AppendVertex(RenderMeshAssetData& mesh, float x, float y, float u, float v) {
    mesh.tangentVertices.push_back(RenderStaticMeshVertexP3N3T4UV2{
        .x = x,
        .y = y,
        .z = 0.0F,
        .nx = 0.0F,
        .ny = 0.0F,
        .nz = 1.0F,
        .tx = 1.0F,
        .ty = 0.0F,
        .tz = 0.0F,
        .tw = 1.0F,
        .u = u,
        .v = v,
    });
}

} // namespace

kb::assets::AssetId BuiltInParticleQuadMeshAssetId() noexcept {
    return kb::assets::MakeAssetId("BuiltIn:ParticleQuadMesh");
}

RenderMeshAssetData BuildBuiltInParticleQuadMesh() {
    RenderMeshAssetData mesh;
    AppendVertex(mesh, -0.5F, -0.5F, 0.0F, 1.0F);
    AppendVertex(mesh, 0.5F, -0.5F, 1.0F, 1.0F);
    AppendVertex(mesh, 0.5F, 0.5F, 1.0F, 0.0F);
    AppendVertex(mesh, -0.5F, 0.5F, 0.0F, 0.0F);
    mesh.indices32 = { 0U, 1U, 2U, 0U, 2U, 3U };
    mesh.materialSlots.push_back(RenderMaterialSlotDesc{});
    mesh.materialNames.push_back("Particle");
    mesh.bounds = RenderBoundsSphere{ .center = { 0.0F, 0.0F, 0.0F }, .radius = 0.70711F };
    mesh.sections.push_back(RenderMeshSectionDesc{
        .indexStart = 0U,
        .indexCount = static_cast<std::uint32_t>(mesh.indices32.size()),
        .materialSlot = 0U,
        .bounds = mesh.bounds,
    });
    mesh.lods.push_back(RenderMeshLodDesc{
        .firstSection = 0U,
        .sectionCount = 1U,
        .minScreenCoverage = 0.0F,
    });
    // Billboards face the camera by construction (SceneParticleRenderSynchronizer's own
    // LookRotation-based model matrix) - doubleSided sidesteps any winding-order assumption
    // about which side ends up front.
    mesh.RefreshDesc().doubleSided = true;
    return mesh;
}

} // namespace kb::render
