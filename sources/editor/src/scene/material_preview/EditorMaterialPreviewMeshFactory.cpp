#include "scene/material_preview/EditorMaterialPreviewMeshFactory.hpp"

#include <cmath>
#include <array>
#include <cstdint>
#include <cstddef>
#include <vector>

namespace kb::editor {
namespace {

constexpr float kPi = 3.14159265358979323846F;

struct Vec3 {
    float x = 0.0F;
    float y = 0.0F;
    float z = 0.0F;
};

struct TangentAccum {
    Vec3 tangent{};
    Vec3 bitangent{};
};

[[nodiscard]] Vec3 Add(Vec3 lhs, Vec3 rhs) noexcept {
    return Vec3{ lhs.x + rhs.x, lhs.y + rhs.y, lhs.z + rhs.z };
}

[[nodiscard]] Vec3 Subtract(Vec3 lhs, Vec3 rhs) noexcept {
    return Vec3{ lhs.x - rhs.x, lhs.y - rhs.y, lhs.z - rhs.z };
}

[[nodiscard]] Vec3 Scale(Vec3 value, float scalar) noexcept {
    return Vec3{ value.x * scalar, value.y * scalar, value.z * scalar };
}

[[nodiscard]] float Dot(Vec3 lhs, Vec3 rhs) noexcept {
    return (lhs.x * rhs.x) + (lhs.y * rhs.y) + (lhs.z * rhs.z);
}

[[nodiscard]] Vec3 Cross(Vec3 lhs, Vec3 rhs) noexcept {
    return Vec3{
        (lhs.y * rhs.z) - (lhs.z * rhs.y),
        (lhs.z * rhs.x) - (lhs.x * rhs.z),
        (lhs.x * rhs.y) - (lhs.y * rhs.x),
    };
}

[[nodiscard]] Vec3 Normalize(Vec3 value) noexcept {
    const float length = std::sqrt(Dot(value, value));
    return length > 0.0001F
        ? Vec3{ value.x / length, value.y / length, value.z / length }
        : Vec3{};
}

[[nodiscard]] std::array<float, 4> FallbackTangentForNormal(float nx, float ny, float nz) noexcept {
    const std::array<float, 3> axis = std::abs(ny) < 0.99F
        ? std::array<float, 3>{ 0.0F, 1.0F, 0.0F }
        : std::array<float, 3>{ 1.0F, 0.0F, 0.0F };
    std::array<float, 3> tangent{
        axis[1] * nz - axis[2] * ny,
        axis[2] * nx - axis[0] * nz,
        axis[0] * ny - axis[1] * nx,
    };
    const float length = std::sqrt(tangent[0] * tangent[0] + tangent[1] * tangent[1] + tangent[2] * tangent[2]);
    if (length > 0.0001F) {
        tangent[0] /= length;
        tangent[1] /= length;
        tangent[2] /= length;
    } else {
        tangent = { 1.0F, 0.0F, 0.0F };
    }
    return { tangent[0], tangent[1], tangent[2], 1.0F };
}

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
    const std::array<float, 4> tangent = FallbackTangentForNormal(nx, ny, nz);
    mesh.tangentVertices.push_back(kb::render::RenderStaticMeshVertexP3N3T4UV2{
        .x = x,
        .y = y,
        .z = z,
        .nx = nx,
        .ny = ny,
        .nz = nz,
        .tx = tangent[0],
        .ty = tangent[1],
        .tz = tangent[2],
        .tw = tangent[3],
        .u = u,
        .v = v,
    });
}

void RecalculateTangentsFromUv(kb::render::RenderMeshAssetData& mesh) {
    if (mesh.tangentVertices.empty() || mesh.indices32.empty()) {
        return;
    }

    std::vector<TangentAccum> accum(mesh.tangentVertices.size());
    for (std::size_t index = 0U; index + 2U < mesh.indices32.size(); index += 3U) {
        const std::uint32_t ia = mesh.indices32[index];
        const std::uint32_t ib = mesh.indices32[index + 1U];
        const std::uint32_t ic = mesh.indices32[index + 2U];
        if (ia >= mesh.tangentVertices.size() || ib >= mesh.tangentVertices.size() || ic >= mesh.tangentVertices.size()) {
            continue;
        }

        const kb::render::RenderStaticMeshVertexP3N3T4UV2& a = mesh.tangentVertices[ia];
        const kb::render::RenderStaticMeshVertexP3N3T4UV2& b = mesh.tangentVertices[ib];
        const kb::render::RenderStaticMeshVertexP3N3T4UV2& c = mesh.tangentVertices[ic];
        const Vec3 p0{ a.x, a.y, a.z };
        const Vec3 p1{ b.x, b.y, b.z };
        const Vec3 p2{ c.x, c.y, c.z };
        const Vec3 edge1 = Subtract(p1, p0);
        const Vec3 edge2 = Subtract(p2, p0);
        const float du1 = b.u - a.u;
        const float dv1 = b.v - a.v;
        const float du2 = c.u - a.u;
        const float dv2 = c.v - a.v;
        const float denominator = (du1 * dv2) - (du2 * dv1);
        if (std::abs(denominator) <= 0.000001F) {
            continue;
        }

        const float scale = 1.0F / denominator;
        const Vec3 tangent = Scale(Subtract(Scale(edge1, dv2), Scale(edge2, dv1)), scale);
        const Vec3 bitangent = Scale(Subtract(Scale(edge2, du1), Scale(edge1, du2)), scale);
        accum[ia].tangent = Add(accum[ia].tangent, tangent);
        accum[ib].tangent = Add(accum[ib].tangent, tangent);
        accum[ic].tangent = Add(accum[ic].tangent, tangent);
        accum[ia].bitangent = Add(accum[ia].bitangent, bitangent);
        accum[ib].bitangent = Add(accum[ib].bitangent, bitangent);
        accum[ic].bitangent = Add(accum[ic].bitangent, bitangent);
    }

    for (std::size_t index = 0U; index < mesh.tangentVertices.size(); ++index) {
        kb::render::RenderStaticMeshVertexP3N3T4UV2& vertex = mesh.tangentVertices[index];
        const Vec3 normal = Normalize(Vec3{ vertex.nx, vertex.ny, vertex.nz });
        Vec3 tangent = Subtract(accum[index].tangent, Scale(normal, Dot(normal, accum[index].tangent)));
        tangent = Normalize(tangent);
        if (Dot(tangent, tangent) <= 0.0001F) {
            const std::array<float, 4> fallback = FallbackTangentForNormal(vertex.nx, vertex.ny, vertex.nz);
            tangent = Vec3{ fallback[0], fallback[1], fallback[2] };
        }
        const float handedness = Dot(Cross(normal, tangent), accum[index].bitangent) < 0.0F ? -1.0F : 1.0F;
        vertex.tx = tangent.x;
        vertex.ty = tangent.y;
        vertex.tz = tangent.z;
        vertex.tw = handedness;
    }
}

void FinalizeMesh(kb::render::RenderMeshAssetData& mesh, float radius) {
    RecalculateTangentsFromUv(mesh);
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
    constexpr std::uint32_t kSegments = 48U;
    constexpr std::uint32_t kRings = 24U;

    kb::render::RenderMeshAssetData mesh;
    mesh.tangentVertices.reserve(static_cast<std::size_t>((kRings + 1U) * (kSegments + 1U)));
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
                mesh.indices32.push_back(d);
                mesh.indices32.push_back(b);
            }
            if (ring + 1U != kRings) {
                mesh.indices32.push_back(d);
                mesh.indices32.push_back(c);
                mesh.indices32.push_back(b);
            }
        }
    }

    FinalizeMesh(mesh, 1.0F);
    return mesh;
}

kb::render::RenderMeshAssetData EditorMaterialPreviewMeshFactory::BuildCylinder() {
    constexpr std::uint32_t kSegments = 48U;
    constexpr float kHalfHeight = 1.0F;

    kb::render::RenderMeshAssetData mesh;
    mesh.tangentVertices.reserve(static_cast<std::size_t>((kSegments + 1U) * 4U + 2U));
    mesh.indices32.reserve(static_cast<std::size_t>(kSegments * 12U));

    for (std::uint32_t segment = 0U; segment <= kSegments; ++segment) {
        const float u = static_cast<float>(segment) / static_cast<float>(kSegments);
        const float theta = u * 2.0F * kPi;
        const float x = std::cos(theta);
        const float z = std::sin(theta);
        AppendVertex(mesh, x, -kHalfHeight, z, x, 0.0F, z, u, 1.0F);
        AppendVertex(mesh, x, kHalfHeight, z, x, 0.0F, z, u, 0.0F);
    }

    for (std::uint32_t segment = 0U; segment < kSegments; ++segment) {
        const std::uint32_t base = segment * 2U;
        mesh.indices32.insert(mesh.indices32.end(), {base, base + 1U, base + 2U, base + 1U, base + 3U, base + 2U});
    }

    const std::uint32_t topCenter = static_cast<std::uint32_t>(mesh.tangentVertices.size());
    AppendVertex(mesh, 0.0F, kHalfHeight, 0.0F, 0.0F, 1.0F, 0.0F, 0.5F, 0.5F);
    const std::uint32_t bottomCenter = static_cast<std::uint32_t>(mesh.tangentVertices.size());
    AppendVertex(mesh, 0.0F, -kHalfHeight, 0.0F, 0.0F, -1.0F, 0.0F, 0.5F, 0.5F);

    const std::uint32_t topStart = static_cast<std::uint32_t>(mesh.tangentVertices.size());
    for (std::uint32_t segment = 0U; segment <= kSegments; ++segment) {
        const float theta = (static_cast<float>(segment) / static_cast<float>(kSegments)) * 2.0F * kPi;
        const float x = std::cos(theta);
        const float z = std::sin(theta);
        AppendVertex(mesh, x, kHalfHeight, z, 0.0F, 1.0F, 0.0F, 0.5F + x * 0.5F, 0.5F - z * 0.5F);
    }
    const std::uint32_t bottomStart = static_cast<std::uint32_t>(mesh.tangentVertices.size());
    for (std::uint32_t segment = 0U; segment <= kSegments; ++segment) {
        const float theta = (static_cast<float>(segment) / static_cast<float>(kSegments)) * 2.0F * kPi;
        const float x = std::cos(theta);
        const float z = std::sin(theta);
        AppendVertex(mesh, x, -kHalfHeight, z, 0.0F, -1.0F, 0.0F, 0.5F + x * 0.5F, 0.5F + z * 0.5F);
    }

    for (std::uint32_t segment = 0U; segment < kSegments; ++segment) {
        mesh.indices32.insert(mesh.indices32.end(), {topCenter, topStart + segment, topStart + segment + 1U});
        mesh.indices32.insert(mesh.indices32.end(), {bottomCenter, bottomStart + segment + 1U, bottomStart + segment});
    }

    FinalizeMesh(mesh, 1.4143F);
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
        const std::uint32_t base = static_cast<std::uint32_t>(mesh.tangentVertices.size());
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

kb::render::RenderMeshAssetData EditorMaterialPreviewMeshFactory::BuildPlane() {
    kb::render::RenderMeshAssetData mesh;
    // The counter-clockwise winding below faces +Z. Keep the authored normals aligned with that
    // front face so one-sided users (including the skeletal preview floor) can orient it reliably.
    AppendVertex(mesh, -1.0F, -1.0F, 0.0F, 0.0F, 0.0F, 1.0F, 0.0F, 1.0F);
    AppendVertex(mesh, 1.0F, -1.0F, 0.0F, 0.0F, 0.0F, 1.0F, 1.0F, 1.0F);
    AppendVertex(mesh, 1.0F, 1.0F, 0.0F, 0.0F, 0.0F, 1.0F, 1.0F, 0.0F);
    AppendVertex(mesh, -1.0F, 1.0F, 0.0F, 0.0F, 0.0F, 1.0F, 0.0F, 0.0F);
    mesh.indices32 = { 0U, 1U, 2U, 0U, 2U, 3U };
    FinalizeMesh(mesh, 1.4143F);
    return mesh;
}

} // namespace kb::editor
