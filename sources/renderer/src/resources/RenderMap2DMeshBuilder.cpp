#include "kb/render/resources/RenderMap2DMeshBuilder.hpp"

#include "engine/assets/AssetId.hpp"
#include "engine/assets/AssetFileName.hpp"
#include "engine/assets/AssetMetadata.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <map>
#include <string>
#include <vector>

namespace kb::render {
namespace {

struct Quad {
    float x0 = 0.0F;
    float z0 = 0.0F;
    float x1 = 0.0F;
    float z1 = 0.0F;
    float y = 0.0F;
    float u0 = 0.0F;
    float v0 = 0.0F;
    float u1 = 0.0F;
    float v1 = 0.0F;
    kb::assets::Map2DSprite style{};
};

class SectionBounds {
public:
    void Add(float x, float y, float z) noexcept {
        min_ = { std::min(min_[0], x), std::min(min_[1], y), std::min(min_[2], z) };
        max_ = { std::max(max_[0], x), std::max(max_[1], y), std::max(max_[2], z) };
    }

    [[nodiscard]] RenderBoundsSphere Sphere() const noexcept {
        const std::array<float, 3> center = Center();
        const std::array<float, 3> half = HalfExtents();
        return RenderBoundsSphere{ .center = center, .radius = std::sqrt(half[0] * half[0] + half[1] * half[1] + half[2] * half[2]) };
    }

    [[nodiscard]] RenderBoundsBox Box() const noexcept {
        return RenderBoundsBox{ .center = Center(), .halfExtents = HalfExtents() };
    }

private:
    [[nodiscard]] std::array<float, 3> Center() const noexcept {
        return { (min_[0] + max_[0]) * 0.5F, (min_[1] + max_[1]) * 0.5F, (min_[2] + max_[2]) * 0.5F };
    }

    [[nodiscard]] std::array<float, 3> HalfExtents() const noexcept {
        return {
            std::max((max_[0] - min_[0]) * 0.5F, 0.001F),
            std::max((max_[1] - min_[1]) * 0.5F, 0.001F),
            std::max((max_[2] - min_[2]) * 0.5F, 0.001F),
        };
    }

    std::array<float, 3> min_{ std::numeric_limits<float>::max(), std::numeric_limits<float>::max(), std::numeric_limits<float>::max() };
    std::array<float, 3> max_{ std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest() };
};

// Map coordinates (a, b along the map's columns and rows, `lift` off its surface) in the mesh.
// The engine is left-handed (a camera looks along its +Z with +X to its right), so a camera
// looking straight down with +X to the right has +Z at the top of the screen: the map's first row
// (north) lies at the largest Z. A wall faces -Z, where a camera with no rotation looks at it.
// Up the image is +Z on the ground and +Y on a wall; the tangent is +X.
struct Placement {
    kb::assets::Map2DOrientation orientation = kb::assets::Map2DOrientation::Flat;
    float mapWidth = 0.0F;
    float mapHeight = 0.0F;
    float drawOffset = 0.0F;

    [[nodiscard]] RenderStaticMeshVertexP3N3T4UV2 Vertex(float a, float b, float lift, float u, float v) const noexcept {
        lift += drawOffset;
        const bool wall = orientation == kb::assets::Map2DOrientation::Wall;
        return RenderStaticMeshVertexP3N3T4UV2{
            .x = a - mapWidth * 0.5F,
            .y = wall ? mapHeight - b : lift,
            .z = wall ? -lift : mapHeight * 0.5F - b,
            .nx = 0.0F, .ny = wall ? 0.0F : 1.0F, .nz = wall ? -1.0F : 0.0F,
            .tx = 1.0F, .ty = 0.0F, .tz = 0.0F, .tw = -1.0F,
            .u = u, .v = v,
            .u1 = u, .v1 = v,
        };
    }
};

void EmitSection(
    RenderMeshAssetData& mesh,
    const std::vector<Quad>& quads,
    std::uint32_t materialSlot,
    const Placement& placement,
    SectionBounds& meshBounds) {
    if (quads.empty()) return;
    const auto firstIndex = static_cast<std::uint32_t>(mesh.indices32.size());
    const auto firstVertex = static_cast<std::uint32_t>(mesh.tangentVertices.size());
    SectionBounds bounds;
    for (const Quad& quad : quads) {
        const auto base = static_cast<std::uint32_t>(mesh.tangentVertices.size()) - firstVertex;
        const auto vertex = [&](float x, float z, float u, float v) {
            const auto uv = kb::assets::Map2DSpriteUV(quad.style, u, v);
            return placement.Vertex(x, z, quad.y, quad.u0 + uv[0] * (quad.u1 - quad.u0), quad.v0 + uv[1] * (quad.v1 - quad.v0));
        };
        const std::array<RenderStaticMeshVertexP3N3T4UV2, 4U> corners{
            vertex(quad.x0, quad.z1, 0, 1), vertex(quad.x1, quad.z1, 1, 1),
            vertex(quad.x1, quad.z0, 1, 0), vertex(quad.x0, quad.z0, 0, 0),
        };
        for (const RenderStaticMeshVertexP3N3T4UV2& corner : corners) {
            mesh.tangentVertices.push_back(corner);
            bounds.Add(corner.x, corner.y, corner.z);
            meshBounds.Add(corner.x, corner.y, corner.z);
        }
        mesh.indices32.insert(mesh.indices32.end(), { base, base + 1U, base + 2U, base, base + 2U, base + 3U });
    }
    mesh.sections.push_back(RenderMeshSectionDesc{
        .indexStart = firstIndex,
        .indexCount = static_cast<std::uint32_t>(mesh.indices32.size()) - firstIndex,
        .vertexStart = firstVertex,
        .vertexCount = static_cast<std::uint32_t>(mesh.tangentVertices.size()) - firstVertex,
        .materialSlot = materialSlot,
        .bounds = bounds.Sphere(),
    });
}

} // namespace

std::uint64_t RenderMap2DMeshBuilder::MaterialAssetId(std::string_view virtualPath) {
    if (virtualPath.empty()) return 0U;
    return kb::assets::MakeAssetId(kb::assets::NormalizeAssetPath(kb::assets::Utf8Path(virtualPath)) + ":RenderMaterial").value;
}

std::optional<RenderMeshAssetData> RenderMap2DMeshBuilder::BuildOccluders(const kb::assets::Map2DAsset& map, float drawOffset, double seconds) {
    auto shadow = map; shadow.tiles.clear(); shadow.background = false; shadow.lit = false;
    for (const auto& entry : map.tiles) {
        const std::uint64_t tileKey = entry.first;
        const kb::assets::Map2DTile& tile = entry.second;
        const auto add = [&map, &shadow, tileKey](kb::assets::Map2DSprite sprite) {
            const auto* definition = kb::assets::Map2DDefinition(map, sprite);
            if (definition && definition->occludesLight) shadow.tiles[tileKey].items.push_back(sprite);
        };
        add(tile.ground); for (auto sprite : tile.items) add(sprite);
    }
    if (shadow.tiles.empty()) return {};
    return Build(shadow, drawOffset + map.tileSize, seconds);
}

std::optional<RenderMeshAssetData> RenderMap2DMeshBuilder::Build(const kb::assets::Map2DAsset& map, float drawOffset, double seconds) {
    if (!kb::assets::IsMap2DAssetValid(map)) return std::nullopt;

    std::vector<std::vector<Quad>> sheetQuads(map.sheets.size());
    std::map<std::pair<std::uint16_t, std::uint32_t>, std::vector<Quad>> tintedQuads;
    const float size = map.tileSize;
    const auto addSprite = [&](kb::assets::Map2DSprite sprite, std::uint32_t x, std::uint32_t y, float lift) {
        if ((sprite.tint & 0xFFU) == 0U) return;
        sprite = kb::assets::Map2DAnimatedSprite(map, sprite, seconds);
        std::uint32_t left = 0U, top = 0U, cellWidth = 0U, cellHeight = 0U;
        if (!kb::assets::Map2DSpriteRect(map, sprite, left, top, cellWidth, cellHeight)) return;
        const auto& sheet = map.sheets[sprite.sheet];
        const float width = static_cast<float>(sheet.ImageWidth()), height = static_cast<float>(sheet.ImageHeight());
        float spriteWidth = 1.0F, spriteHeight = static_cast<float>(cellHeight) / static_cast<float>(cellWidth);
        if (sprite.rotation % 2U) std::swap(spriteWidth, spriteHeight);
        auto& quads = sprite.tint == 0xFFFFFFFFU && !map.lit ? sheetQuads[sprite.sheet] : tintedQuads[{sprite.sheet, sprite.tint}];
        quads.push_back(Quad{
            .x0 = (static_cast<float>(x) + (1.0F - spriteWidth) * 0.5F + sprite.offsetX) * size,
            .z0 = (static_cast<float>(y + 1U) - spriteHeight + sprite.offsetY) * size,
            .x1 = (static_cast<float>(x) + (1.0F + spriteWidth) * 0.5F + sprite.offsetX) * size,
            .z1 = (static_cast<float>(y + 1U) + sprite.offsetY) * size,
            .y = lift,
            .u0 = (static_cast<float>(left) + 0.5F) / width,
            .v0 = (static_cast<float>(top) + 0.5F) / height,
            .u1 = (static_cast<float>(left + cellWidth) - 0.5F) / width,
            .v1 = (static_cast<float>(top + cellHeight) - 0.5F) / height,
            .style = sprite,
        });
    };
    for (const auto& [key, tile] : map.tiles) {
        const std::uint32_t x = kb::assets::Map2DTileKeyX(key);
        const std::uint32_t y = kb::assets::Map2DTileKeyY(key);
        const float floor = static_cast<float>(kb::assets::Map2DTileKeyZ(key)) * map.floorHeight +
            (map.sortByY ? 0.4F * static_cast<float>(y) / static_cast<float>(map.height + 1U) : 0.0F);
        const float layerLift = map.sortByY ? 0.4F / static_cast<float>((map.height + 1U) * 64U) : kLayerLift;
        std::uint32_t layer = 1U;
        if (tile.ground.IsValid()) addSprite(tile.ground, x, y, floor + layerLift * static_cast<float>(layer));
        ++layer;
        for (const kb::assets::Map2DSprite sprite : tile.borders) addSprite(sprite, x, y, floor + layerLift * static_cast<float>(layer++));
        for (const kb::assets::Map2DSprite sprite : tile.items) addSprite(sprite, x, y, floor + layerLift * static_cast<float>(layer++));
    }

    RenderMeshAssetData mesh{};
    SectionBounds bounds;
    const Placement placement{ map.orientation, static_cast<float>(map.width) * size, static_cast<float>(map.height) * size, drawOffset };
    if (map.background) {
        // Under the lowest floor painted, so floors below the ground floor still show.
        const std::int32_t lowest = map.tiles.empty() ? 0 : std::min(0, kb::assets::Map2DTileKeyZ(map.tiles.begin()->first));
        EmitSection(mesh, { Quad{
            .x1 = static_cast<float>(map.width) * size,
            .z1 = static_cast<float>(map.height) * size,
            .y = static_cast<float>(lowest) * map.floorHeight - kLayerLift,
            .u1 = 1.0F,
            .v1 = 1.0F,
        } }, 0U, placement, bounds);
    }
    for (std::size_t sheet = 0U; sheet < sheetQuads.size(); ++sheet) {
        EmitSection(mesh, sheetQuads[sheet], static_cast<std::uint32_t>(sheet + 1U), placement, bounds);
    }
    std::uint32_t tintSlot = static_cast<std::uint32_t>(map.sheets.size() + 1U);
    for (const auto& [key, quads] : tintedQuads) { static_cast<void>(key); EmitSection(mesh, quads, tintSlot++, placement, bounds); }
    if (mesh.sections.empty()) {
        // Keep the mesh contract valid without drawing a phantom tile on an empty layer.
        EmitSection(mesh, { Quad{ .u1 = 1.0F, .v1 = 1.0F } }, 0U, placement, bounds);
    }

    mesh.materialSlots.push_back(RenderMaterialSlotDesc{});
    mesh.materialNames.emplace_back("Map Background");
    RenderMeshEmbeddedMaterial background{};
    background.name = "Map Background";
    background.desc.baseColor[0] = 0.02F;
    background.desc.baseColor[1] = 0.02F;
    background.desc.baseColor[2] = 0.025F;
    background.desc.baseColor[3] = 1.0F;
    background.desc.roughnessFactor = 1.0F;
    background.desc.doubleSided = true;
    mesh.embeddedMaterials.push_back(std::move(background));
    for (const kb::assets::Map2DSheet& sheet : map.sheets) {
        mesh.materialSlots.push_back(RenderMaterialSlotDesc{ .defaultMaterialAssetId = MaterialAssetId(sheet.material) });
        mesh.materialNames.push_back(std::filesystem::path{ sheet.texture }.stem().string());
        // Slots and embedded materials pair up one to one; this one shows only without the sheet's material.
        RenderMeshEmbeddedMaterial fallback{};
        fallback.name = mesh.materialNames.back();
        fallback.desc.doubleSided = true;
        fallback.desc.baseColor[0] = fallback.desc.baseColor[1] = fallback.desc.baseColor[2] = 0;
        fallback.desc.metallicFactor = 1;
        fallback.desc.roughnessFactor = 1;
        fallback.desc.emissiveColor[0] = fallback.desc.emissiveColor[1] = fallback.desc.emissiveColor[2] = 1;
        fallback.desc.emissiveStrength = 1;
        fallback.desc.alphaMode = RenderMaterialAlphaMode::Mask;
        fallback.desc.alphaCutoff = 0.5F;
        fallback.desc.albedoTextureAssetId = fallback.desc.emissiveTextureAssetId = kb::assets::MakeAssetId(
            kb::assets::NormalizeAssetPath(kb::assets::Utf8Path(sheet.texture)) + ":RenderTexture").value;
        fallback.albedoTexturePath = fallback.emissiveTexturePath = sheet.texture;
        mesh.embeddedMaterials.push_back(std::move(fallback));
    }
    for (const auto& [key, quads] : tintedQuads) {
        static_cast<void>(quads);
        const auto& sheet = map.sheets[key.first];
        const auto tint = key.second;
        RenderMeshEmbeddedMaterial material{};
        material.name = "Tinted Tile";
        auto& desc = material.desc;
        desc.baseColor[0] = desc.baseColor[1] = desc.baseColor[2] = 0.0F;
        desc.baseColor[3] = static_cast<float>(tint & 0xFFU) / 255.0F;
        desc.emissiveColor[0] = static_cast<float>((tint >> 24U) & 0xFFU) / 255.0F;
        desc.emissiveColor[1] = static_cast<float>((tint >> 16U) & 0xFFU) / 255.0F;
        desc.emissiveColor[2] = static_cast<float>((tint >> 8U) & 0xFFU) / 255.0F;
        desc.emissiveStrength = 1.0F; desc.metallicFactor = 1.0F; desc.roughnessFactor = 1.0F;
        if (map.lit) {
            for (unsigned channel = 0; channel < 3U; ++channel) { desc.baseColor[channel] = desc.emissiveColor[channel]; desc.emissiveColor[channel] = 0; }
            desc.emissiveStrength = 0; desc.metallicFactor = 0;
        }
        desc.alphaMode = (tint & 0xFFU) == 255U ? RenderMaterialAlphaMode::Mask : RenderMaterialAlphaMode::Blend;
        desc.alphaCutoff = 0.5F; desc.doubleSided = true;
        desc.albedoTextureAssetId = desc.emissiveTextureAssetId = kb::assets::MakeAssetId(kb::assets::NormalizeAssetPath(kb::assets::Utf8Path(sheet.texture)) + ":RenderTexture").value;
        material.albedoTexturePath = material.emissiveTexturePath = sheet.texture;
        mesh.materialSlots.push_back(RenderMaterialSlotDesc{});
        mesh.materialNames.push_back(material.name);
        mesh.embeddedMaterials.push_back(std::move(material));
    }
    mesh.bounds = bounds.Sphere();
    mesh.boundsBox = bounds.Box();
    mesh.lods.push_back(RenderMeshLodDesc{
        .firstSection = 0U,
        .sectionCount = static_cast<std::uint32_t>(mesh.sections.size()),
        .minScreenCoverage = 0.0F,
    });
    static_cast<void>(mesh.RefreshDesc());
    return mesh;
}

} // namespace kb::render
