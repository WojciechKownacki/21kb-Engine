#include "kb/render/resources/ShapeMeshes.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace kb::render {
namespace {

constexpr float kPi = 3.14159265358979323846F;
constexpr std::uint32_t kSegments = 48U;
constexpr std::uint32_t kRings = 24U;

struct Vec3 {
    float x = 0.0F;
    float y = 0.0F;
    float z = 0.0F;
};

[[nodiscard]] Vec3 Add(Vec3 a, Vec3 b) noexcept { return Vec3{ a.x + b.x, a.y + b.y, a.z + b.z }; }
[[nodiscard]] Vec3 Subtract(Vec3 a, Vec3 b) noexcept { return Vec3{ a.x - b.x, a.y - b.y, a.z - b.z }; }
[[nodiscard]] Vec3 Scale(Vec3 a, float s) noexcept { return Vec3{ a.x * s, a.y * s, a.z * s }; }
[[nodiscard]] float Dot(Vec3 a, Vec3 b) noexcept { return a.x * b.x + a.y * b.y + a.z * b.z; }
[[nodiscard]] Vec3 Cross(Vec3 a, Vec3 b) noexcept {
    return Vec3{ a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x };
}
[[nodiscard]] Vec3 Normalize(Vec3 a) noexcept {
    const float length = std::sqrt(Dot(a, a));
    return length > 0.0001F ? Scale(a, 1.0F / length) : Vec3{};
}

[[nodiscard]] Vec3 FallbackTangent(Vec3 normal) noexcept {
    const Vec3 axis = std::abs(normal.y) < 0.99F ? Vec3{ 0.0F, 1.0F, 0.0F } : Vec3{ 1.0F, 0.0F, 0.0F };
    const Vec3 tangent = Normalize(Cross(axis, normal));
    return Dot(tangent, tangent) > 0.0F ? tangent : Vec3{ 1.0F, 0.0F, 0.0F };
}

class Builder {
public:
    std::uint32_t Vertex(Vec3 position, Vec3 normal, float u, float v) {
        const Vec3 tangent = FallbackTangent(normal);
        mesh_.tangentVertices.push_back(RenderStaticMeshVertexP3N3T4UV2{
            .x = position.x, .y = position.y, .z = position.z,
            .nx = normal.x, .ny = normal.y, .nz = normal.z,
            .tx = tangent.x, .ty = tangent.y, .tz = tangent.z, .tw = 1.0F,
            .u = u, .v = v,
        });
        return static_cast<std::uint32_t>(mesh_.tangentVertices.size() - 1U);
    }

    void Triangle(std::uint32_t a, std::uint32_t b, std::uint32_t c) { mesh_.indices32.insert(mesh_.indices32.end(), { a, b, c }); }

    // Rows of vertices (each kSegments + 1 wide, top to bottom) stitched into a closed surface
    // around Y; the first and last rows are poles when `poles` is set.
    void StitchRows(std::uint32_t firstVertex, std::uint32_t rowCount, bool poles) {
        const std::uint32_t stride = kSegments + 1U;
        for (std::uint32_t row = 0U; row + 1U < rowCount; ++row) {
            for (std::uint32_t segment = 0U; segment < kSegments; ++segment) {
                const std::uint32_t a = firstVertex + row * stride + segment;
                const std::uint32_t b = a + stride;
                const std::uint32_t c = b + 1U;
                const std::uint32_t d = a + 1U;
                if (!poles || row != 0U) Triangle(a, d, b);
                if (!poles || row + 2U != rowCount) Triangle(d, c, b);
            }
        }
    }

    // A disc of `radius` at height y, facing up or down.
    void Disc(float radius, float y, bool up) {
        const Vec3 normal{ 0.0F, up ? 1.0F : -1.0F, 0.0F };
        const std::uint32_t center = Vertex(Vec3{ 0.0F, y, 0.0F }, normal, 0.5F, 0.5F);
        const std::uint32_t start = static_cast<std::uint32_t>(mesh_.tangentVertices.size());
        for (std::uint32_t segment = 0U; segment <= kSegments; ++segment) {
            const float theta = static_cast<float>(segment) / static_cast<float>(kSegments) * 2.0F * kPi;
            const float x = std::cos(theta);
            const float z = std::sin(theta);
            static_cast<void>(Vertex(Vec3{ x * radius, y, z * radius }, normal, 0.5F + x * 0.5F, 0.5F + (up ? -z : z) * 0.5F));
        }
        for (std::uint32_t segment = 0U; segment < kSegments; ++segment) {
            if (up) Triangle(center, start + segment + 1U, start + segment);
            else Triangle(center, start + segment, start + segment + 1U);
        }
    }

    RenderMeshAssetData Finish(Vec3 halfExtents, std::string_view materialName) {
        RecalculateTangents();
        mesh_.materialSlots.push_back(RenderMaterialSlotDesc{});
        mesh_.materialNames.emplace_back(materialName);
        mesh_.boundsBox = RenderBoundsBox{ .center = { 0.0F, 0.0F, 0.0F },
            .halfExtents = { halfExtents.x, std::max(halfExtents.y, 0.001F), std::max(halfExtents.z, 0.001F) } };
        mesh_.bounds = RenderBoundsSphere{ .center = { 0.0F, 0.0F, 0.0F }, .radius = std::sqrt(Dot(halfExtents, halfExtents)) };
        mesh_.sections.push_back(RenderMeshSectionDesc{
            .indexStart = 0U,
            .indexCount = static_cast<std::uint32_t>(mesh_.indices32.size()),
            .materialSlot = 0U,
            .bounds = mesh_.bounds,
        });
        mesh_.lods.push_back(RenderMeshLodDesc{ .firstSection = 0U, .sectionCount = 1U, .minScreenCoverage = 0.0F });
        static_cast<void>(mesh_.RefreshDesc());
        return std::move(mesh_);
    }

private:
    // Tangents follow the UVs, so normal maps read the same on every shape.
    void RecalculateTangents() {
        std::vector<std::array<Vec3, 2>> accum(mesh_.tangentVertices.size());
        for (std::size_t index = 0U; index + 2U < mesh_.indices32.size(); index += 3U) {
            const std::uint32_t ids[3] = { mesh_.indices32[index], mesh_.indices32[index + 1U], mesh_.indices32[index + 2U] };
            const auto& a = mesh_.tangentVertices[ids[0]];
            const auto& b = mesh_.tangentVertices[ids[1]];
            const auto& c = mesh_.tangentVertices[ids[2]];
            const Vec3 edge1 = Subtract(Vec3{ b.x, b.y, b.z }, Vec3{ a.x, a.y, a.z });
            const Vec3 edge2 = Subtract(Vec3{ c.x, c.y, c.z }, Vec3{ a.x, a.y, a.z });
            const float du1 = b.u - a.u, dv1 = b.v - a.v, du2 = c.u - a.u, dv2 = c.v - a.v;
            const float denominator = du1 * dv2 - du2 * dv1;
            if (std::abs(denominator) <= 0.000001F) continue;
            const Vec3 tangent = Scale(Subtract(Scale(edge1, dv2), Scale(edge2, dv1)), 1.0F / denominator);
            const Vec3 bitangent = Scale(Subtract(Scale(edge2, du1), Scale(edge1, du2)), 1.0F / denominator);
            for (const std::uint32_t id : ids) {
                accum[id][0] = Add(accum[id][0], tangent);
                accum[id][1] = Add(accum[id][1], bitangent);
            }
        }
        for (std::size_t index = 0U; index < mesh_.tangentVertices.size(); ++index) {
            auto& vertex = mesh_.tangentVertices[index];
            const Vec3 normal = Normalize(Vec3{ vertex.nx, vertex.ny, vertex.nz });
            Vec3 tangent = Normalize(Subtract(accum[index][0], Scale(normal, Dot(normal, accum[index][0]))));
            if (Dot(tangent, tangent) <= 0.0001F) tangent = FallbackTangent(normal);
            vertex.tx = tangent.x;
            vertex.ty = tangent.y;
            vertex.tz = tangent.z;
            vertex.tw = Dot(Cross(normal, tangent), accum[index][1]) < 0.0F ? -1.0F : 1.0F;
        }
    }

    RenderMeshAssetData mesh_;
};

} // namespace

RenderMeshAssetData ShapeMeshes::BuiltIn(kb::assets::BuiltInShape shape) {
    constexpr std::string_view kMaterial = "Default";
    switch (shape) {
    case kb::assets::BuiltInShape::Cube: return Box(0.5F, kMaterial);
    case kb::assets::BuiltInShape::Sphere: return Sphere(0.5F, kMaterial);
    case kb::assets::BuiltInShape::Capsule: return Capsule(0.5F, 2.0F, kMaterial);
    case kb::assets::BuiltInShape::Cylinder: return Cylinder(0.5F, 2.0F, kMaterial);
    case kb::assets::BuiltInShape::Cone: return Cone(0.5F, 1.0F, kMaterial);
    case kb::assets::BuiltInShape::Plane: return Ground(10.0F, 10, kMaterial);
    case kb::assets::BuiltInShape::Quad: return Quad(1.0F, true, kMaterial);
    }
    return Box(0.5F, kMaterial);
}

RenderMeshAssetData ShapeMeshes::Box(float h, std::string_view materialName) {
    Builder builder;
    const auto face = [&builder, h](Vec3 a, Vec3 b, Vec3 c, Vec3 d, Vec3 normal) {
        const std::uint32_t ia = builder.Vertex(Scale(a, h), normal, 0.0F, 1.0F);
        const std::uint32_t ib = builder.Vertex(Scale(b, h), normal, 1.0F, 1.0F);
        const std::uint32_t ic = builder.Vertex(Scale(c, h), normal, 1.0F, 0.0F);
        const std::uint32_t id = builder.Vertex(Scale(d, h), normal, 0.0F, 0.0F);
        builder.Triangle(ia, ib, ic);
        builder.Triangle(ia, ic, id);
    };
    face({ -1, -1, 1 }, { 1, -1, 1 }, { 1, 1, 1 }, { -1, 1, 1 }, { 0, 0, 1 });
    face({ 1, -1, -1 }, { -1, -1, -1 }, { -1, 1, -1 }, { 1, 1, -1 }, { 0, 0, -1 });
    face({ -1, 1, 1 }, { 1, 1, 1 }, { 1, 1, -1 }, { -1, 1, -1 }, { 0, 1, 0 });
    face({ -1, -1, -1 }, { 1, -1, -1 }, { 1, -1, 1 }, { -1, -1, 1 }, { 0, -1, 0 });
    face({ 1, -1, 1 }, { 1, -1, -1 }, { 1, 1, -1 }, { 1, 1, 1 }, { 1, 0, 0 });
    face({ -1, -1, -1 }, { -1, -1, 1 }, { -1, 1, 1 }, { -1, 1, -1 }, { -1, 0, 0 });
    return builder.Finish(Vec3{ h, h, h }, materialName);
}

RenderMeshAssetData ShapeMeshes::Sphere(float radius, std::string_view materialName) {
    Builder builder;
    for (std::uint32_t ring = 0U; ring <= kRings; ++ring) {
        const float v = static_cast<float>(ring) / static_cast<float>(kRings);
        const float phi = v * kPi;
        for (std::uint32_t segment = 0U; segment <= kSegments; ++segment) {
            const float u = static_cast<float>(segment) / static_cast<float>(kSegments);
            const float theta = u * 2.0F * kPi;
            const Vec3 normal{ std::sin(phi) * std::cos(theta), std::cos(phi), std::sin(phi) * std::sin(theta) };
            static_cast<void>(builder.Vertex(Scale(normal, radius), normal, u, 1.0F - v));
        }
    }
    builder.StitchRows(0U, kRings + 1U, true);
    return builder.Finish(Vec3{ radius, radius, radius }, materialName);
}

RenderMeshAssetData ShapeMeshes::Capsule(float radius, float height, std::string_view materialName) {
    // Two hemispheres of a sphere pushed apart by the straight part, like Unity's capsule.
    Builder builder;
    const float straightHalf = std::max(0.0F, height * 0.5F - radius);
    constexpr std::uint32_t kHemisphereRings = kRings / 2U;
    const std::uint32_t rowCount = (kHemisphereRings + 1U) * 2U;
    for (std::uint32_t row = 0U; row < rowCount; ++row) {
        const bool top = row <= kHemisphereRings;
        const std::uint32_t ring = top ? row : row - 1U;
        const float phi = static_cast<float>(ring) / static_cast<float>(kRings) * kPi;
        const float v = static_cast<float>(row) / static_cast<float>(rowCount - 1U);
        for (std::uint32_t segment = 0U; segment <= kSegments; ++segment) {
            const float u = static_cast<float>(segment) / static_cast<float>(kSegments);
            const float theta = u * 2.0F * kPi;
            const Vec3 normal{ std::sin(phi) * std::cos(theta), std::cos(phi), std::sin(phi) * std::sin(theta) };
            const Vec3 position = Add(Scale(normal, radius), Vec3{ 0.0F, top ? straightHalf : -straightHalf, 0.0F });
            static_cast<void>(builder.Vertex(position, normal, u, 1.0F - v));
        }
    }
    builder.StitchRows(0U, rowCount, true);
    return builder.Finish(Vec3{ radius, height * 0.5F, radius }, materialName);
}

RenderMeshAssetData ShapeMeshes::Cylinder(float radius, float height, std::string_view materialName) {
    Builder builder;
    const float half = height * 0.5F;
    for (const float y : { half, -half }) {
        for (std::uint32_t segment = 0U; segment <= kSegments; ++segment) {
            const float u = static_cast<float>(segment) / static_cast<float>(kSegments);
            const float theta = u * 2.0F * kPi;
            const Vec3 normal{ std::cos(theta), 0.0F, std::sin(theta) };
            static_cast<void>(builder.Vertex(Vec3{ normal.x * radius, y, normal.z * radius }, normal, u, y > 0.0F ? 0.0F : 1.0F));
        }
    }
    builder.StitchRows(0U, 2U, false);
    builder.Disc(radius, half, true);
    builder.Disc(radius, -half, false);
    return builder.Finish(Vec3{ radius, half, radius }, materialName);
}

RenderMeshAssetData ShapeMeshes::Cone(float radius, float height, std::string_view materialName) {
    Builder builder;
    const float half = height * 0.5F;
    for (const float y : { half, -half }) {
        for (std::uint32_t segment = 0U; segment <= kSegments; ++segment) {
            const float u = static_cast<float>(segment) / static_cast<float>(kSegments);
            const float theta = u * 2.0F * kPi;
            const Vec3 normal = Normalize(Vec3{ std::cos(theta) * height, radius, std::sin(theta) * height });
            const float ring = y > 0.0F ? 0.0F : radius; // the top row is the tip
            static_cast<void>(builder.Vertex(Vec3{ std::cos(theta) * ring, y, std::sin(theta) * ring }, normal, u, y > 0.0F ? 0.0F : 1.0F));
        }
    }
    builder.StitchRows(0U, 2U, false);
    builder.Disc(radius, -half, false);
    return builder.Finish(Vec3{ radius, half, radius }, materialName);
}

RenderMeshAssetData ShapeMeshes::Ground(float size, int divisions, std::string_view materialName) {
    Builder builder;
    const int cells = std::max(1, divisions);
    const Vec3 up{ 0.0F, 1.0F, 0.0F };
    for (int row = 0; row <= cells; ++row) {
        for (int column = 0; column <= cells; ++column) {
            const float u = static_cast<float>(column) / static_cast<float>(cells);
            const float v = static_cast<float>(row) / static_cast<float>(cells);
            static_cast<void>(builder.Vertex(Vec3{ (u - 0.5F) * size, 0.0F, (v - 0.5F) * size }, up, u, 1.0F - v));
        }
    }
    const std::uint32_t stride = static_cast<std::uint32_t>(cells + 1);
    for (std::uint32_t row = 0U; row < static_cast<std::uint32_t>(cells); ++row) {
        for (std::uint32_t column = 0U; column < static_cast<std::uint32_t>(cells); ++column) {
            const std::uint32_t a = row * stride + column;
            const std::uint32_t b = a + 1U;
            const std::uint32_t d = a + stride;
            const std::uint32_t c = d + 1U;
            builder.Triangle(a, c, b);
            builder.Triangle(a, d, c);
        }
    }
    return builder.Finish(Vec3{ size * 0.5F, 0.0F, size * 0.5F }, materialName);
}

RenderMeshAssetData ShapeMeshes::Quad(float size, bool facesNegativeZ, std::string_view materialName) {
    Builder builder;
    const float h = size * 0.5F;
    const Vec3 normal{ 0.0F, 0.0F, facesNegativeZ ? -1.0F : 1.0F };
    const std::uint32_t a = builder.Vertex(Vec3{ -h, -h, 0.0F }, normal, 0.0F, 1.0F);
    const std::uint32_t b = builder.Vertex(Vec3{ h, -h, 0.0F }, normal, 1.0F, 1.0F);
    const std::uint32_t c = builder.Vertex(Vec3{ h, h, 0.0F }, normal, 1.0F, 0.0F);
    const std::uint32_t d = builder.Vertex(Vec3{ -h, h, 0.0F }, normal, 0.0F, 0.0F);
    if (facesNegativeZ) {
        builder.Triangle(a, c, b);
        builder.Triangle(a, d, c);
    } else {
        builder.Triangle(a, b, c);
        builder.Triangle(a, c, d);
    }
    return builder.Finish(Vec3{ h, h, 0.0F }, materialName);
}

} // namespace kb::render
