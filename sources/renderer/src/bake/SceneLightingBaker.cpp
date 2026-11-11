#include "kb/render/bake/SceneLightingBaker.hpp"

#include "engine/assets/AssetImportTypes.hpp"
#include "engine/assets/AssetManager.hpp"
#include "engine/assets/AssetRegistry.hpp"
#include "engine/render/StaticLightmapBaker.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneAssets.hpp"
#include "engine/scene/SceneComponents.hpp"
#include "engine/scene/SceneEntities.hpp"
#include "engine/scene/SceneHierarchyAccess.hpp"
#include "engine/scene/SceneLightComponents.hpp"
#include "engine/scene/SceneMeshRendererComponents.hpp"
#include "engine/scene/SceneRuntime.hpp"
#include "engine/scene/SceneTransforms.hpp"
#include "kb/render/resources/RenderMaterialAssetWriter.hpp"
#include "kb/render/resources/RenderMeshAssetBuilder.hpp"
#include "kb/render/resources/RenderMeshImportArtifacts.hpp"
#include "kb/render/runtime/RuntimeRenderAssetDiscovery.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <set>

namespace kb::render {
namespace {
using Vec3 = kb::math::Vec3;

void Collect(const kb::scene::Scene& scene, kb::scene::SceneEntity entity, std::vector<kb::scene::SceneEntity>& out) {
    out.push_back(entity);
    for (const auto child : scene.Hierarchy().ChildEntities(entity)) Collect(scene, child, out);
}

// One corner of a source triangle in the mesh's local space.
struct Corner { Vec3 position, normal; float u = 0, v = 0, u1 = 0, v1 = 0; };
using LocalTriangle = std::array<Corner, 3>;

struct Receiver {
    kb::scene::SceneEntity entity{};
    std::string name;
    std::vector<LocalTriangle> local;
    std::vector<LightmapTriangle> world;
    bool generate = false;
    StaticLightmapResult baked;
    kb::render::RenderMaterialAssetData material;
    std::uint64_t meshAssetId = 0;
};

std::string SafeName(std::string_view text) {
    std::string out;
    for (const char c : text) out += (std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-') ? c : '_';
    return out.empty() ? "Entity" : out;
}

void Append(std::string& bytes, const void* data, std::size_t size) { bytes.append(static_cast<const char*>(data), size); }

// A binary glTF with one unindexed primitive: positions, normals, UV0 and UV1 (the lightmap set). The source
// importers mirror X and flip the winding to reach engine space, so this writes the mirrored form back.
std::string BuildGlb(const std::vector<LocalTriangle>& triangles, const std::vector<float>& lightmapUv) {
    const std::size_t count = triangles.size() * 3U;
    std::string bin;
    std::array<float, 3> low{ 1e30F, 1e30F, 1e30F }, high{ -1e30F, -1e30F, -1e30F };
    std::vector<float> position, normal, uv0, uv1;
    for (std::size_t t = 0; t < triangles.size(); ++t) {
        // Corners 1 and 2 swap places: the winding flip.
        for (const std::size_t k : { std::size_t{ 0 }, std::size_t{ 2 }, std::size_t{ 1 } }) {
            const Corner& c = triangles[t][k];
            const float p[]{ -c.position.x, c.position.y, c.position.z };
            for (int i = 0; i < 3; ++i) { low[static_cast<std::size_t>(i)] = std::min(low[static_cast<std::size_t>(i)], p[i]); high[static_cast<std::size_t>(i)] = std::max(high[static_cast<std::size_t>(i)], p[i]); }
            position.insert(position.end(), { p[0], p[1], p[2] });
            normal.insert(normal.end(), { -c.normal.x, c.normal.y, c.normal.z });
            uv0.insert(uv0.end(), { c.u, c.v });
            uv1.insert(uv1.end(), { lightmapUv[t * 6 + k * 2], lightmapUv[t * 6 + k * 2 + 1] });
        }
    }
    std::vector<std::uint32_t> indices(count);
    for (std::size_t i = 0; i < count; ++i) indices[i] = static_cast<std::uint32_t>(i);
    const std::size_t o1 = position.size() * 4U, o2 = o1 + normal.size() * 4U, o3 = o2 + uv0.size() * 4U, o4 = o3 + uv1.size() * 4U;
    Append(bin, position.data(), o1); Append(bin, normal.data(), normal.size() * 4U); Append(bin, uv0.data(), uv0.size() * 4U);
    Append(bin, uv1.data(), uv1.size() * 4U); Append(bin, indices.data(), indices.size() * 4U);
    const auto number = [](float value) { return std::to_string(value); };
    std::string json = R"({"asset":{"version":"2.0"},"buffers":[{"byteLength":)" + std::to_string(bin.size()) + R"(}],"bufferViews":[)";
    json += R"({"buffer":0,"byteOffset":0,"byteLength":)" + std::to_string(o1) + R"(},)";
    json += R"({"buffer":0,"byteOffset":)" + std::to_string(o1) + R"(,"byteLength":)" + std::to_string(o2 - o1) + R"(},)";
    json += R"({"buffer":0,"byteOffset":)" + std::to_string(o2) + R"(,"byteLength":)" + std::to_string(o3 - o2) + R"(},)";
    json += R"({"buffer":0,"byteOffset":)" + std::to_string(o3) + R"(,"byteLength":)" + std::to_string(o4 - o3) + R"(},)";
    json += R"({"buffer":0,"byteOffset":)" + std::to_string(o4) + R"(,"byteLength":)" + std::to_string(count * 4U) + R"(}],"accessors":[)";
    json += R"({"bufferView":0,"componentType":5126,"count":)" + std::to_string(count) + R"(,"type":"VEC3","min":[)" + number(low[0]) + "," + number(low[1]) + "," + number(low[2]) +
        R"(],"max":[)" + number(high[0]) + "," + number(high[1]) + "," + number(high[2]) + "]},";
    json += R"({"bufferView":1,"componentType":5126,"count":)" + std::to_string(count) + R"(,"type":"VEC3"},)";
    json += R"({"bufferView":2,"componentType":5126,"count":)" + std::to_string(count) + R"(,"type":"VEC2"},)";
    json += R"({"bufferView":3,"componentType":5126,"count":)" + std::to_string(count) + R"(,"type":"VEC2"},)";
    json += R"({"bufferView":4,"componentType":5125,"count":)" + std::to_string(count) + R"(,"type":"SCALAR"}],)";
    json += R"("meshes":[{"primitives":[{"attributes":{"POSITION":0,"NORMAL":1,"TEXCOORD_0":2,"TEXCOORD_1":3},"indices":4}]}],"nodes":[{"mesh":0}],"scenes":[{"nodes":[0]}],"scene":0})";
    while (json.size() % 4U) json += ' ';
    while (bin.size() % 4U) bin += '\0';
    std::string glb;
    const std::uint32_t header[]{ 0x46546C67U, 2U, static_cast<std::uint32_t>(12U + 8U + json.size() + 8U + bin.size()),
        static_cast<std::uint32_t>(json.size()), 0x4E4F534AU };
    Append(glb, header, sizeof(header)); glb += json;
    const std::uint32_t binHeader[]{ static_cast<std::uint32_t>(bin.size()), 0x004E4942U };
    Append(glb, binHeader, sizeof(binHeader)); glb += bin;
    return glb;
}

// Uncompressed top-origin BGRA TGA: the ordinary texture loader and cooker own its runtime format.
bool WriteTga(const std::filesystem::path& path, std::uint16_t size, const std::vector<std::uint8_t>& rgba) {
    std::array<unsigned char, 18> header{};
    header[2] = 2; header[12] = static_cast<unsigned char>(size & 255); header[13] = static_cast<unsigned char>(size >> 8);
    header[14] = header[12]; header[15] = header[13]; header[16] = 32; header[17] = 0x28;
    std::ofstream image(path, std::ios::binary);
    image.write(reinterpret_cast<const char*>(header.data()), header.size());
    for (std::size_t pixel = 0; pixel < rgba.size(); pixel += 4) {
        const char bgra[]{ static_cast<char>(rgba[pixel + 2]), static_cast<char>(rgba[pixel + 1]), static_cast<char>(rgba[pixel]), static_cast<char>(rgba[pixel + 3]) };
        image.write(bgra, 4);
    }
    image.close();
    return static_cast<bool>(image);
}
} // namespace

SceneLightingBakeResult BakeSceneLighting(kb::scene::Scene& scene, std::string_view sceneName,
    const SceneLightingBakeSettings& settings, const SceneLightingBakeProgress& progress) {
    SceneLightingBakeResult result;
    const auto fail = [&](std::string text) { result.error = std::move(text); return result; };
    if (settings.resolution < 16 || settings.resolution > 1024 || settings.bounces > 4) return fail("resolution must be 16..1024 and bounces 0..4");
    const std::string folderName = SafeName(sceneName);
    const std::string virtualFolder = "/Game/Baked/" + folderName;
    kb::render::RuntimeRenderAssetDiscovery discovery;
    discovery.SetDiscoveryEnabled(false);
    discovery.Ensure(scene, 0);
    auto& assets = scene.Assets().Manager();
    const auto folder = assets.Mounts().Resolve(std::filesystem::path{ virtualFolder });
    if (!folder.has_value()) return fail("the project content is not mounted as /Game");
    scene.Runtime().SynchronizeTransforms();

    std::vector<kb::scene::SceneEntity> entities;
    for (const auto root : scene.Hierarchy().RootEntities()) Collect(scene, root, entities);

    std::vector<LightmapLight> lights;
    std::vector<LightmapTriangle> occluders;
    std::vector<Receiver> receivers;
    std::set<std::string> usedNames;
    const auto albedoOf = [&](std::uint64_t materialAssetId) {
        if (materialAssetId == 0) return Vec3{ 0.8F, 0.8F, 0.8F };
        const auto material = assets.Load<kb::render::RenderMaterialAssetData>(kb::assets::AssetId{ materialAssetId });
        if (!material.IsLoaded()) return Vec3{ 0.8F, 0.8F, 0.8F };
        const auto& color = material.Shared()->desc.baseColor;
        return Vec3{ color[0], color[1], color[2] };
    };
    for (const auto entity : entities) {
        if (!scene.Entities().IsActive(entity)) continue;
        const auto* transform = scene.Transforms().TryGet(entity);
        if (transform == nullptr) continue;
        const auto position = scene.Transforms().WorldTranslation(entity, *transform);
        if (std::max({ std::abs(position.x), std::abs(position.y), std::abs(position.z) }) > 1000000.0) return fail("bake geometry exceeds supported world bounds");
        const Vec3 translation{ static_cast<float>(position.x), static_cast<float>(position.y), static_cast<float>(position.z) };
        if (const auto* light = scene.Components().Lights().TryGet(entity); light != nullptr && light->mobility == kb::scene::LightMobility::Static) {
            if (light->kind == kb::scene::LightKind::Spot) { ++result.skippedLights; }
            else if ((light->kind != kb::scene::LightKind::Point && light->kind != kb::scene::LightKind::Directional) || light->useColorTemperature) {
                return fail("lightmap supports static point/directional lights with RGB colour");
            } else {
                lights.push_back({ .position = translation, .direction = kb::math::Rotate(transform->worldRotation, Vec3{ 0, 0, 1 }),
                    .color = light->color, .intensity = light->intensity, .range = light->range,
                    .directional = light->kind == kb::scene::LightKind::Directional, .castsShadow = light->castsShadow });
            }
        }
        const auto* mesh = scene.Components().MeshRenderers().TryGet(entity);
        if (mesh == nullptr || mesh->shadowOnly || mesh->transient) continue;
        const bool isReceiver = mesh->mobility == kb::scene::MeshMobility::Static;
        if (!isReceiver && !mesh->castsShadow) continue;
        const auto source = assets.Load<kb::render::RenderMeshAssetData>(kb::assets::AssetId{ mesh->meshAssetId });
        if (!source.IsLoaded()) { if (isReceiver) return fail("static bake mesh cannot be loaded"); continue; }
        const auto& data = *source.Shared();
        if (transform->worldScale.x == 0 || transform->worldScale.y == 0 || transform->worldScale.z == 0) {
            if (isReceiver) return fail("bake mesh has zero scale");
            continue;
        }
        if (isReceiver && (mesh->materialSlotOverrideCount != 0 || mesh->materialInstanceHandle != 0)) return fail("a static receiver needs one mesh material");
        const Vec3 albedo = albedoOf(mesh->materialAssetId);
        Receiver receiver;
        if (isReceiver) {
            receiver.entity = entity;
            receiver.name = SafeName(scene.Entities().Name(entity));
            for (int suffix = 2; !usedNames.insert(receiver.name).second; ++suffix) receiver.name = SafeName(scene.Entities().Name(entity)) + "_" + std::to_string(suffix);
            receiver.meshAssetId = mesh->meshAssetId;
            if (mesh->materialAssetId != 0) {
                const auto original = assets.Load<kb::render::RenderMaterialAssetData>(kb::assets::AssetId{ mesh->materialAssetId });
                if (!original.IsLoaded()) return fail("receiver material cannot be loaded for " + receiver.name);
                receiver.material = *original.Shared();
                const auto& m = receiver.material;
                // A previous bake of this entity is allowed: its material only adds the lightmap.
                if (!m.desc.bakedLightmapUv1 && (m.materialType != "builtin.pbr" || !m.graph.nodes.empty() || m.desc.metallicFactor != 0 ||
                    m.desc.alphaMode != kb::render::RenderMaterialAlphaMode::Opaque || m.desc.transmissionFactor != 0 || m.desc.emissiveTextureAssetId != 0 ||
                    !m.emissiveTexturePath.empty() || m.desc.normalTextureAssetId != 0 || !m.normalTexturePath.empty() || m.desc.occlusionTextureAssetId != 0 ||
                    !m.occlusionTexturePath.empty() || m.desc.clearcoatFactor != 0 || m.desc.anisotropyStrength != 0 || m.desc.sheenColor[0] != 0 ||
                    m.desc.sheenColor[1] != 0 || m.desc.sheenColor[2] != 0 ||
                    (m.desc.emissiveStrength > 0 && (m.desc.emissiveColor[0] != 0 || m.desc.emissiveColor[1] != 0 || m.desc.emissiveColor[2] != 0)))) {
                    return fail("bake receiver " + receiver.name + " must use opaque matte builtin PBR without emission or normal maps");
                }
            }
        }
        const auto indexCount = data.indices32.empty() ? data.indices16.size() : data.indices32.size();
        const auto vertexCount = data.vertices.empty() ? data.tangentVertices.size() : data.vertices.size();
        for (const auto& section : data.sections) {
            if (section.lodLevel != 0) continue;
            if (section.terrainLayerIndex != UINT8_MAX || section.indexCount % 3 != 0 || static_cast<std::size_t>(section.indexStart) + section.indexCount > indexCount) {
                return fail("unsupported or invalid bake geometry");
            }
            for (std::size_t index = section.indexStart; index < section.indexStart + section.indexCount; index += 3) {
                LocalTriangle localTriangle;
                LightmapTriangle worldTriangle;
                for (unsigned corner = 0; corner < 3; ++corner) {
                    const auto local = data.indices32.empty() ? data.indices16[index + corner] : data.indices32[index + corner];
                    const auto global = static_cast<std::size_t>(section.vertexStart) + local;
                    if (global >= vertexCount) return fail("invalid bake vertex index");
                    const auto read = [&](const auto& v) {
                        localTriangle[corner] = { { v.x, v.y, v.z }, { v.nx, v.ny, v.nz }, v.u, v.v, v.u1, v.v1 };
                    };
                    if (data.vertices.empty()) read(data.tangentVertices[global]); else read(data.vertices[global]);
                    const Corner& c = localTriangle[corner];
                    const auto p = kb::math::Rotate(transform->worldRotation, Vec3{ c.position.x * transform->worldScale.x, c.position.y * transform->worldScale.y, c.position.z * transform->worldScale.z });
                    worldTriangle[corner].position = { translation.x + p.x, translation.y + p.y, translation.z + p.z };
                    worldTriangle[corner].normal = kb::math::Rotate(transform->worldRotation, Vec3{ c.normal.x / transform->worldScale.x, c.normal.y / transform->worldScale.y, c.normal.z / transform->worldScale.z });
                    worldTriangle[corner].u = c.u1; worldTriangle[corner].v = c.v1;
                    worldTriangle[corner].albedo = albedo;
                }
                if (mesh->castsShadow) occluders.push_back(worldTriangle);
                if (isReceiver) { receiver.local.push_back(localTriangle); receiver.world.push_back(worldTriangle); }
            }
        }
        if (isReceiver) {
            if (receiver.world.empty()) return fail("receiver " + receiver.name + " has no triangles");
            receivers.push_back(std::move(receiver));
        }
    }
    if (receivers.empty()) return fail("no static mesh renderer to bake; set a mesh renderer's mobility to Static");
    if (lights.empty()) return fail("no static point/directional light to bake");
    result.lights = static_cast<std::uint32_t>(lights.size());

    // Phase 1: everything that can fail or be cancelled, without touching the project.
    for (std::size_t index = 0; index < receivers.size(); ++index) {
        Receiver& receiver = receivers[index];
        if (progress && !progress(static_cast<float>(index) / static_cast<float>(receivers.size()), receiver.name)) { result.cancelled = true; return result; }
        if (!HasUsableLightmapUv(receiver.world, settings.resolution)) {
            // The mesh becomes unindexed: one vertex per triangle corner, each chart free to use its own texels.
            const auto layout = GenerateLightmapUv(receiver.world, settings.resolution);
            if (!layout.Succeeded()) return fail(receiver.name + ": " + layout.error);
            for (std::size_t t = 0; t < receiver.world.size(); ++t) for (std::size_t k = 0; k < 3; ++k) {
                receiver.world[t][k].u = layout.uv[t * 6 + k * 2]; receiver.world[t][k].v = layout.uv[t * 6 + k * 2 + 1];
                receiver.local[t][k].u1 = receiver.world[t][k].u; receiver.local[t][k].v1 = receiver.world[t][k].v;
            }
            receiver.generate = true;
        }
        StaticLightmapSettings bake;
        bake.resolution = settings.resolution;
        bake.bounces = settings.bounces;
        receiver.baked = BakeStaticLightmap(receiver.world, occluders, lights, bake);
        if (!receiver.baked.Succeeded()) return fail(receiver.name + ": " + receiver.baked.error);
    }
    if (progress && !progress(1.0F, "write")) { result.cancelled = true; return result; }

    // Phase 2: write the lightmaps, derived meshes and materials, then rebind the entities.
    std::error_code code;
    std::filesystem::create_directories(*folder, code);
    if (code) return fail("cannot create the baked lighting folder");
    for (Receiver& receiver : receivers) {
        const auto texturePath = *folder / (receiver.name + ".tga");
        const auto materialPath = *folder / (receiver.name + ".kbmat");
        if (!WriteTga(texturePath, settings.resolution, receiver.baked.rgba)) return fail("cannot write " + texturePath.generic_string());
        receiver.material.desc.bakedLightmap = true;
        receiver.material.desc.bakedLightmapUv1 = true;
        receiver.material.desc.emissiveTextureAssetId = 0;
        receiver.material.emissiveTexturePath = virtualFolder + "/" + receiver.name + ".tga";
        if (!kb::render::RenderMaterialAssetWriter::Save(materialPath, receiver.material)) return fail("cannot write " + materialPath.generic_string());
        if (receiver.generate) {
            std::vector<float> uv(receiver.local.size() * 6);
            for (std::size_t t = 0; t < receiver.local.size(); ++t) for (std::size_t k = 0; k < 3; ++k) { uv[t * 6 + k * 2] = receiver.local[t][k].u1; uv[t * 6 + k * 2 + 1] = receiver.local[t][k].v1; }
            // The imported asset takes the source's file name.
            const auto temporaryFolder = std::filesystem::temp_directory_path(code) / "kb_lightmap" / folderName;
            std::filesystem::create_directories(temporaryFolder, code);
            const auto temporary = temporaryFolder / (receiver.name + "_LightmapUv.glb");
            const std::string glb = BuildGlb(receiver.local, uv);
            { std::ofstream out(temporary, std::ios::binary); out.write(glb.data(), static_cast<std::streamsize>(glb.size())); if (!out) return fail("cannot write the generated mesh source"); }
            kb::assets::AssetImportOptions options;
            options.mesh.importMaterials = false; options.mesh.importTextures = false;
            options.conflictPolicy = kb::assets::AssetImportConflictPolicy::Overwrite;
            const std::filesystem::path sources[]{ temporary };
            const auto imported = kb::render::RenderMeshImportArtifacts::ImportStaticFiles(assets, sources, std::filesystem::path{ virtualFolder }, options);
            std::filesystem::remove(temporary, code);
            if (imported.items.empty() || !imported.items.front().Succeeded()) return fail("cannot import the generated lightmap mesh for " + receiver.name);
            receiver.meshAssetId = imported.items.front().id.value;
            ++result.generatedMeshes;
        }
    }
    static_cast<void>(assets.DiscoverMountedAssets());
    for (const Receiver& receiver : receivers) {
        const auto* material = assets.Registry().FindByPath(std::filesystem::path{ virtualFolder + "/" + receiver.name + ".kbmat" });
        if (material == nullptr) return fail("baked material for " + receiver.name + " was not registered");
        auto renderer = *scene.Components().MeshRenderers().TryGet(receiver.entity);
        renderer.meshAssetId = receiver.meshAssetId;
        renderer.materialAssetId = material->id.value;
        scene.Components().MeshRenderers().Set(receiver.entity, renderer);
        ++result.receivers;
        result.texels += receiver.baked.coveredTexels;
    }
    return result;
}

} // namespace kb::render
