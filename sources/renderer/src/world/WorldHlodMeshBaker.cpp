#include "kb/render/world/WorldHlodMeshBaker.hpp"

#include "engine/assets/AssetManager.hpp"
#include "kb/render/resources/RenderMeshAssetBuilder.hpp"

#include <meshoptimizer/src/meshoptimizer.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <map>
#include <string>
#include <vector>

namespace kb::render {
namespace {

// Welded vertex of the merged proxy; only exact duplicates are joined.
struct ProxyVertex {
    float x = 0.0F;
    float y = 0.0F;
    float z = 0.0F;
    float nx = 0.0F;
    float ny = 0.0F;
    float nz = 0.0F;
    float u = 0.0F;
    float v = 0.0F;
};

struct Bucket {
    std::uint64_t material = 0U;
    std::vector<ProxyVertex> vertices;
    std::vector<unsigned int> indices;
};

constexpr std::size_t kMaxSlots = 8U;

struct SourceVertex {
    float x, y, z, nx, ny, nz, u, v;
};

[[nodiscard]] SourceVertex ReadVertex(const RenderMeshAssetData& mesh, std::size_t index) {
    if (!mesh.vertices.empty()) {
        const RenderStaticMeshVertexP3N3UV2& vertex = mesh.vertices[index];
        return { vertex.x, vertex.y, vertex.z, vertex.nx, vertex.ny, vertex.nz, vertex.u, vertex.v };
    }
    const RenderStaticMeshVertexP3N3T4UV2& vertex = mesh.tangentVertices[index];
    return { vertex.x, vertex.y, vertex.z, vertex.nx, vertex.ny, vertex.nz, vertex.u, vertex.v };
}

[[nodiscard]] std::size_t VertexCount(const RenderMeshAssetData& mesh) {
    return mesh.vertices.empty() ? mesh.tangentVertices.size() : mesh.vertices.size();
}

[[nodiscard]] std::uint32_t ReadIndex(const RenderMeshAssetData& mesh, std::size_t index) {
    return mesh.indices32.empty() ? mesh.indices16[index] : mesh.indices32[index];
}

[[nodiscard]] std::size_t IndexCount(const RenderMeshAssetData& mesh) {
    return mesh.indices32.empty() ? mesh.indices16.size() : mesh.indices32.size();
}

// Normal matrix: the inverse transpose of the linear part, so non-uniform scale
// keeps normals perpendicular to their surface.
[[nodiscard]] std::array<double, 9U> NormalMatrix(const std::array<double, 12U>& m) {
    const double a = m[0], b = m[3], c = m[6];
    const double d = m[1], e = m[4], f = m[7];
    const double g = m[2], h = m[5], i = m[8];
    // Cofactor matrix equals determinant * inverse transpose; the scale drops out
    // when the normal is renormalized.
    return { e * i - f * h, -(b * i - c * h), b * f - c * e,
             -(d * i - f * g), a * i - c * g, -(a * f - c * d),
             d * h - e * g, -(a * h - b * g), a * e - b * d };
}

[[nodiscard]] std::uint64_t SectionMaterial(const kb::world::WorldHlodMeshInstance& instance, const RenderMeshAssetData& mesh, std::uint32_t slot) {
    if (slot < instance.slotMaterialCount && slot < instance.slotMaterials.size() && instance.slotMaterials[slot] != 0U) {
        return instance.slotMaterials[slot];
    }
    if (instance.rendererMaterial != 0U) {
        return instance.rendererMaterial;
    }
    return slot < mesh.materialSlots.size() ? mesh.materialSlots[slot].defaultMaterialAssetId : 0U;
}

void AppendFloat(std::string& out, float value) {
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "%.7g", static_cast<double>(value));
    out += buffer;
}

} // namespace

WorldHlodMeshBaker::WorldHlodMeshBaker(kb::assets::AssetManager& assets) noexcept
    : assets_(assets) {}

kb::world::WorldHlodResult WorldHlodMeshBaker::Bake(const kb::world::WorldHlodRequest& request) {
    kb::world::WorldHlodResult result;
    std::map<std::uint64_t, Bucket> buckets;
    std::size_t skipped = 0U;
    for (const kb::world::WorldHlodMeshInstance& instance : request.instances) {
        auto cached = meshes_.find(instance.meshAssetId);
        if (cached == meshes_.end()) {
            const kb::assets::AssetHandle<RenderMeshAssetData> handle = assets_.Load<RenderMeshAssetData>(kb::assets::AssetId{ instance.meshAssetId });
            cached = meshes_.emplace(instance.meshAssetId, handle.IsLoaded() ? handle.Shared() : nullptr).first;
        }
        if (cached->second == nullptr) {
            ++skipped;
            continue;
        }
        const RenderMeshAssetData& mesh = *cached->second;
        const std::array<double, 12U>& m = instance.transform;
        const std::array<double, 9U> normal = NormalMatrix(m);
        const bool mirrored = (m[0] * (m[4] * m[8] - m[7] * m[5]) - m[3] * (m[1] * m[8] - m[7] * m[2]) + m[6] * (m[1] * m[5] - m[4] * m[2])) < 0.0;
        const std::size_t vertexCount = VertexCount(mesh);
        const std::size_t indexCount = IndexCount(mesh);
        for (const RenderMeshSectionDesc& section : mesh.sections) {
            if (section.lodLevel != 0U || section.terrainLayerIndex != UINT8_MAX || section.indexCount < 3U) {
                continue;
            }
            Bucket& bucket = buckets[SectionMaterial(instance, mesh, section.materialSlot)];
            const std::size_t triangleEnd = static_cast<std::size_t>(section.indexStart) + section.indexCount - section.indexCount % 3U;
            for (std::size_t index = section.indexStart; index + 3U <= triangleEnd && index + 3U <= indexCount; index += 3U) {
                std::array<unsigned int, 3U> corners{};
                const std::size_t before = bucket.vertices.size();
                bool valid = true;
                for (std::size_t corner = 0U; corner < 3U; ++corner) {
                    const std::size_t source = static_cast<std::size_t>(section.vertexStart) + ReadIndex(mesh, index + corner);
                    if (source >= vertexCount) {
                        valid = false;
                        break;
                    }
                    const SourceVertex vertex = ReadVertex(mesh, source);
                    const double px = m[0] * vertex.x + m[3] * vertex.y + m[6] * vertex.z + m[9];
                    const double py = m[1] * vertex.x + m[4] * vertex.y + m[7] * vertex.z + m[10];
                    const double pz = m[2] * vertex.x + m[5] * vertex.y + m[8] * vertex.z + m[11];
                    double nx = normal[0] * vertex.nx + normal[3] * vertex.ny + normal[6] * vertex.nz;
                    double ny = normal[1] * vertex.nx + normal[4] * vertex.ny + normal[7] * vertex.nz;
                    double nz = normal[2] * vertex.nx + normal[5] * vertex.ny + normal[8] * vertex.nz;
                    const double length = std::sqrt(nx * nx + ny * ny + nz * nz);
                    if (length > 1e-12) {
                        nx /= length; ny /= length; nz /= length;
                    }
                    corners[corner] = static_cast<unsigned int>(bucket.vertices.size());
                    bucket.vertices.push_back({ static_cast<float>(px), static_cast<float>(py), static_cast<float>(pz),
                        static_cast<float>(nx), static_cast<float>(ny), static_cast<float>(nz), vertex.u, vertex.v });
                }
                if (!valid) {
                    bucket.vertices.resize(before);
                    continue;
                }
                // A mirroring transform flips the winding; restore it.
                if (mirrored) std::swap(corners[1], corners[2]);
                bucket.indices.insert(bucket.indices.end(), corners.begin(), corners.end());
                result.sourceTriangleCount += 1U;
            }
        }
    }
    std::vector<Bucket> ordered;
    for (auto& [material, bucket] : buckets) {
        if (!bucket.indices.empty()) {
            bucket.material = material;
            ordered.push_back(std::move(bucket));
        }
    }
    if (ordered.empty()) {
        result.error = skipped != 0U ? std::to_string(skipped) + " mesh(es) could not be loaded" : std::string{};
        return result;
    }
    // Largest materials keep their own slot; the rest share the last one.
    std::ranges::stable_sort(ordered, [](const Bucket& left, const Bucket& right) { return left.indices.size() > right.indices.size(); });
    while (ordered.size() > kMaxSlots) {
        Bucket extra = std::move(ordered.back());
        ordered.pop_back();
        Bucket& target = ordered[kMaxSlots - 1U];
        const unsigned int base = static_cast<unsigned int>(target.vertices.size());
        target.vertices.insert(target.vertices.end(), extra.vertices.begin(), extra.vertices.end());
        for (const unsigned int index : extra.indices) target.indices.push_back(base + index);
    }

    std::string obj = "# HLOD proxy for cell " + std::to_string(request.coord.x) + "," + std::to_string(request.coord.z) + "\n";
    std::size_t vertexBase = 1U;
    const float ratio = static_cast<float>(std::clamp(request.triangleRatio, 0.0, 1.0));
    for (std::size_t slot = 0U; slot < ordered.size(); ++slot) {
        Bucket& bucket = ordered[slot];
        // Weld exact duplicates so the simplifier sees connected surfaces.
        std::vector<unsigned int> remap(bucket.vertices.size());
        const std::size_t unique = meshopt_generateVertexRemap(remap.data(), bucket.indices.data(), bucket.indices.size(),
            bucket.vertices.data(), bucket.vertices.size(), sizeof(ProxyVertex));
        std::vector<ProxyVertex> welded(unique);
        meshopt_remapVertexBuffer(welded.data(), bucket.vertices.data(), bucket.vertices.size(), sizeof(ProxyVertex), remap.data());
        std::vector<unsigned int> indices(bucket.indices.size());
        meshopt_remapIndexBuffer(indices.data(), bucket.indices.data(), bucket.indices.size(), remap.data());
        const std::size_t target = std::max<std::size_t>(3U, static_cast<std::size_t>(static_cast<float>(indices.size() / 3U) * ratio) * 3U);
        std::vector<unsigned int> simplified(indices.size());
        float error = 0.0F;
        simplified.resize(meshopt_simplify(simplified.data(), indices.data(), indices.size(), &welded[0].x, welded.size(), sizeof(ProxyVertex),
            target, 0.1F, 0, &error));
        if (simplified.empty()) {
            simplified = indices;
        }
        const std::size_t used = meshopt_optimizeVertexFetch(welded.data(), simplified.data(), simplified.size(), welded.data(), welded.size(), sizeof(ProxyVertex));
        welded.resize(used);
        for (const ProxyVertex& vertex : welded) {
            obj += "v "; AppendFloat(obj, vertex.x); obj += ' '; AppendFloat(obj, vertex.y); obj += ' '; AppendFloat(obj, vertex.z); obj += '\n';
            obj += "vt "; AppendFloat(obj, vertex.u); obj += ' '; AppendFloat(obj, vertex.v); obj += '\n';
            obj += "vn "; AppendFloat(obj, vertex.nx); obj += ' '; AppendFloat(obj, vertex.ny); obj += ' '; AppendFloat(obj, vertex.nz); obj += '\n';
        }
        obj += "usemtl slot" + std::to_string(slot) + "\n";
        for (std::size_t index = 0U; index + 2U < simplified.size(); index += 3U) {
            obj += 'f';
            for (std::size_t corner = 0U; corner < 3U; ++corner) {
                const std::string reference = std::to_string(vertexBase + simplified[index + corner]);
                obj += ' ' + reference + '/' + reference + '/' + reference;
            }
            obj += '\n';
        }
        vertexBase += welded.size();
        result.triangleCount += static_cast<std::uint32_t>(simplified.size() / 3U);
        result.materials.push_back(bucket.material);
    }
    result.objText = std::move(obj);
    result.succeeded = true;
    return result;
}

} // namespace kb::render
