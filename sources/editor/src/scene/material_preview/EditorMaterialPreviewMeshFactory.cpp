#include "scene/material_preview/EditorMaterialPreviewMeshFactory.hpp"

#include <cmath>
#include <cstdint>
#include <cstddef>

namespace kb::editor {
namespace {

constexpr float kPi = 3.14159265358979323846F;

void AppendVertex(
    kb::render::RenderMeshAssetData& mesh,
    float x,
    float y,
    float z,
    float nx,
    float ny,
    float nz,
    float u,
    float v) {
    mesh.vertices.push_back(kb::render::RenderStaticMeshVertexP3N3UV2{
        .x = x,
        .y = y,
        .z = z,
        .nx = nx,
        .ny = ny,
        .nz = nz,
        .u = u,
        .v = v,
    });
}

void FinalizeMesh(kb::render::RenderMeshAssetData& mesh, float radius) {
    mesh.materialSlots.push_back(kb::render::RenderMaterialSlotDesc{});
    mesh.materialNames.push_back("Preview");
    mesh.bounds = kb::render::RenderBoundsSphere{
        .center = {0.0F, 0.0F, 0.0F},
        .radius = radius,
    };
    mesh.sections.push_back(kb::render::RenderMeshSectionDesc{
        .indexStart = 0U,
        .indexCount = static_cast<std::uint32_t>(mesh.indices32.size()),
        .materialSlot = 0U,
        .bounds = mesh.bounds,
    });
    mesh.lods.push_back(kb::render::RenderMeshLodDesc{
        .firstSection = 0U,
        .sectionCount = 1U,
        .minScreenCoverage = 0.0F,
    });
    static_cast<void>(mesh.RefreshDesc());
}

} // namespace

kb::render::RenderMeshAssetData EditorMaterialPreviewMeshFactory::BuildSphere() {
    constexpr std::uint32_t kSegments = 32U;
    constexpr std::uint32_t kRings = 16U;

    kb::render::RenderMeshAssetData mesh;
    mesh.vertices.reserve(static_cast<std::size_t>((kRings + 1U) * (kSegments + 1U)));
    mesh.indices32.reserve(static_cast<std::size_t>(kRings * kSegments * 6U));

    for (std::uint32_t ring = 0U; ring <= kRings; ++ring) {
        const float v = static_cast<float>(ring) / static_cast<float>(kRings);
        const float phi = v * kPi;
        const float y = std::cos(phi);
        const float radius = std::sin(phi);
        for (std::uint32_t segment = 0U; segment <= kSegments; ++segment) {
            const float u = static_cast<float>(segment) / static_cast<float>(kSegments);
            const float theta = u * 2.0F * kPi;
            const float x = radius * std::cos(theta);
            const float z = radius * std::sin(theta);
            AppendVertex(mesh, x, y, z, x, y, z, u, 1.0F - v);
        }
    }

    const std::uint32_t stride = kSegments + 1U;
    for (std::uint32_t ring = 0U; ring < kRings; ++ring) {
        for (std::uint32_t segment = 0U; segment < kSegments; ++segment) {
            const std::uint32_t a = ring * stride + segment;
            const std::uint32_t b = a + stride;
            const std::uint32_t c = b + 1U;
            const std::uint32_t d = a + 1U;
            if (ring != 0U) {
                mesh.indices32.push_back(a);
                mesh.indices32.push_back(b);
                mesh.indices32.push_back(d);
            }
            if (ring + 1U != kRings) {
                mesh.indices32.push_back(d);
                mesh.indices32.push_back(b);
                mesh.indices32.push_back(c);
            }
        }
    }

    FinalizeMesh(mesh, 1.0F);
    return mesh;
}

kb::render::RenderMeshAssetData EditorMaterialPreviewMeshFactory::BuildCube() {
    kb::render::RenderMeshAssetData mesh;
    const auto face = [&mesh](
                          float ax,
                          float ay,
                          float az,
                          float bx,
                          float by,
                          float bz,
                          float cx,
                          float cy,
                          float cz,
                          float dx,
                          float dy,
                          float dz,
                          float nx,
                          float ny,
                          float nz) {
        const std::uint32_t base = static_cast<std::uint32_t>(mesh.vertices.size());
        AppendVertex(mesh, ax, ay, az, nx, ny, nz, 0.0F, 1.0F);
        AppendVertex(mesh, bx, by, bz, nx, ny, nz, 1.0F, 1.0F);
        AppendVertex(mesh, cx, cy, cz, nx, ny, nz, 1.0F, 0.0F);
        AppendVertex(mesh, dx, dy, dz, nx, ny, nz, 0.0F, 0.0F);
        mesh.indices32.insert(mesh.indices32.end(), {base, base + 1U, base + 2U, base, base + 2U, base + 3U});
    };

    face(-1.0F, -1.0F, 1.0F, 1.0F, -1.0F, 1.0F, 1.0F, 1.0F, 1.0F, -1.0F, 1.0F, 1.0F, 0.0F, 0.0F, 1.0F);
    face(1.0F, -1.0F, -1.0F, -1.0F, -1.0F, -1.0F, -1.0F, 1.0F, -1.0F, 1.0F, 1.0F, -1.0F, 0.0F, 0.0F, -1.0F);
    face(-1.0F, 1.0F, 1.0F, 1.0F, 1.0F, 1.0F, 1.0F, 1.0F, -1.0F, -1.0F, 1.0F, -1.0F, 0.0F, 1.0F, 0.0F);
    face(-1.0F, -1.0F, -1.0F, 1.0F, -1.0F, -1.0F, 1.0F, -1.0F, 1.0F, -1.0F, -1.0F, 1.0F, 0.0F, -1.0F, 0.0F);
    face(1.0F, -1.0F, 1.0F, 1.0F, -1.0F, -1.0F, 1.0F, 1.0F, -1.0F, 1.0F, 1.0F, 1.0F, 1.0F, 0.0F, 0.0F);
    face(-1.0F, -1.0F, -1.0F, -1.0F, -1.0F, 1.0F, -1.0F, 1.0F, 1.0F, -1.0F, 1.0F, -1.0F, -1.0F, 0.0F, 0.0F);

    FinalizeMesh(mesh, 1.7321F);
    return mesh;
}

} // namespace kb::editor
