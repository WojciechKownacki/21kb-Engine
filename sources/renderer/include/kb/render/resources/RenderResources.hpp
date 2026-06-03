#pragma once

#include "kb/render/resources/RenderHandles.hpp"

#include <bgfx/bgfx.h>

#include <array>
#include <cstdint>
#include <vector>

namespace kb::render {

struct RenderStaticMeshVertex {
    float x = 0.0F;
    float y = 0.0F;
    float z = 0.0F;
    float r = 1.0F;
    float g = 1.0F;
    float b = 1.0F;
};

struct RenderStaticMeshVertexP3N3UV2 {
    float x = 0.0F;
    float y = 0.0F;
    float z = 0.0F;
    float nx = 0.0F;
    float ny = 1.0F;
    float nz = 0.0F;
    float u = 0.0F;
    float v = 0.0F;
    float r = 1.0F;
    float g = 1.0F;
    float b = 1.0F;
};

struct RenderStaticMeshVertexP3N3T4UV2 {
    float x = 0.0F;
    float y = 0.0F;
    float z = 0.0F;
    float nx = 0.0F;
    float ny = 1.0F;
    float nz = 0.0F;
    float tx = 1.0F;
    float ty = 0.0F;
    float tz = 0.0F;
    float tw = 1.0F;
    float u = 0.0F;
    float v = 0.0F;
    float r = 1.0F;
    float g = 1.0F;
    float b = 1.0F;
};

struct RenderStaticMeshVertexSkinned {
    float x = 0.0F;
    float y = 0.0F;
    float z = 0.0F;
    float nx = 0.0F;
    float ny = 1.0F;
    float nz = 0.0F;
    float tx = 1.0F;
    float ty = 0.0F;
    float tz = 0.0F;
    float tw = 1.0F;
    float u = 0.0F;
    float v = 0.0F;
    float r = 1.0F;
    float g = 1.0F;
    float b = 1.0F;
    std::uint16_t joints[4]{};
    float weights[4]{ 1.0F, 0.0F, 0.0F, 0.0F };
};

using RenderVertexP3C3 = RenderStaticMeshVertex;

enum class RenderVertexFormat : std::uint8_t {
    P3C3,
    P3N3UV2,
    P3N3T4UV2,
    // Layout is reserved for a later skinning runtime; static scene mesh registration rejects it.
    SkinnedP3N3T4UV2J4W4,
};

enum class RenderIndexFormat : std::uint8_t {
    Uint16,
    Uint32,
};

enum class RenderMaterialAlphaMode : std::uint8_t {
    Opaque,
    Mask,
    Blend,
};

enum class RenderMaterialDecalBlendMode : std::uint8_t {
    Disabled,
    BaseColor,
    Normal,
    Pbr,
};

enum class RenderMaterialLayerBlendMode : std::uint8_t {
    Replace,
    Add,
    Multiply,
};

struct RenderBoundsSphere {
    std::array<float, 3> center{};
    float radius = 0.0F;

    [[nodiscard]] constexpr bool IsValid() const noexcept {
        return radius > 0.0F;
    }
};

struct RenderMeshSectionDesc {
    std::uint32_t indexStart = 0;
    std::uint32_t indexCount = 0;
    std::uint32_t materialSlot = 0;
    RenderBoundsSphere bounds{};
    std::uint8_t lodLevel = 0;
};

struct RenderMeshSection {
    std::uint32_t indexStart = 0;
    std::uint32_t indexCount = 0;
    std::uint32_t materialSlot = 0;
    RenderBoundsSphere bounds{};
    std::uint8_t lodLevel = 0;
};

struct RenderMeshletDesc {
    std::uint32_t indexStart = 0;
    std::uint32_t indexCount = 0;
    std::uint32_t vertexStart = 0;
    std::uint32_t vertexCount = 0;
    std::uint32_t sectionIndex = 0;
    RenderBoundsSphere bounds{};
    std::array<float, 4> cone{};
    std::uint8_t lodLevel = 0;

    [[nodiscard]] constexpr bool IsValid() const noexcept {
        return indexCount > 0U && vertexCount > 0U && bounds.IsValid();
    }
};

struct RenderMeshLodDesc {
    std::uint32_t firstSection = 0;
    std::uint32_t sectionCount = 0;
    std::uint32_t firstMeshlet = 0;
    std::uint32_t meshletCount = 0;
    float minScreenCoverage = 0.0F;

    [[nodiscard]] constexpr bool IsValid() const noexcept {
        return sectionCount > 0U || meshletCount > 0U;
    }
};

struct RenderGpuDrivenMeshDesc {
    const RenderMeshletDesc* meshlets = nullptr;
    std::uint32_t meshletCount = 0;
    const RenderMeshLodDesc* lods = nullptr;
    std::uint32_t lodCount = 0;
    bool allowGpuCulling = true;
    bool allowIndirectDraws = true;
    bool allowMeshletCulling = true;
};

struct RenderMaterialSlotDesc {
    std::uint64_t defaultMaterialAssetId = 0;
};

struct RenderMaterialSlot {
    std::uint64_t defaultMaterialAssetId = 0;
};

struct RenderMeshDesc {
    const RenderStaticMeshVertex* vertices = nullptr;
    const void* vertexData = nullptr;
    std::uint32_t vertexCount = 0;
    const std::uint16_t* indices = nullptr;
    const std::uint32_t* indices32 = nullptr;
    std::uint32_t indexCount = 0;
    RenderVertexFormat vertexFormat = RenderVertexFormat::P3C3;
    RenderIndexFormat indexFormat = RenderIndexFormat::Uint16;
    const RenderMeshSectionDesc* sections = nullptr;
    std::uint32_t sectionCount = 0;
    const RenderMaterialSlotDesc* materialSlots = nullptr;
    std::uint32_t materialSlotCount = 0;
    RenderBoundsSphere bounds{};
    RenderGpuDrivenMeshDesc gpuDriven{};
    std::uint64_t rasterStateExtra = 0;
    bool doubleSided = false;
};

struct RenderMeshResource {
    bgfx::VertexBufferHandle vertexBuffer = BGFX_INVALID_HANDLE;
    bgfx::IndexBufferHandle indexBuffer = BGFX_INVALID_HANDLE;
    std::uint32_t vertexCount = 0;
    std::uint32_t indexCount = 0;
    RenderVertexFormat vertexFormat = RenderVertexFormat::P3C3;
    RenderIndexFormat indexFormat = RenderIndexFormat::Uint16;
    std::vector<RenderMeshSection> sections;
    std::vector<RenderMaterialSlot> materialSlots;
    std::vector<RenderMeshletDesc> meshlets;
    std::vector<RenderMeshLodDesc> lods;
    RenderBoundsSphere bounds{};
    std::uint64_t rasterStateExtra = 0;
    bool doubleSided = false;
    bool gpuCullingEnabled = false;
    bool indirectDrawsEnabled = false;
    bool meshletCullingEnabled = false;
};

[[nodiscard]] bgfx::VertexLayout RenderStaticMeshVertexLayout();
[[nodiscard]] bgfx::VertexLayout RenderStaticMeshVertexLayout(RenderVertexFormat format);
[[nodiscard]] std::uint32_t RenderStaticMeshVertexStride(RenderVertexFormat format) noexcept;

struct RenderMaterialDesc {
    float baseColor[4]{ 1.0F, 1.0F, 1.0F, 1.0F };
    float emissiveColor[3]{ 0.0F, 0.0F, 0.0F };
    float metallicFactor = 0.0F;
    float roughnessFactor = 1.0F;
    float normalScale = 1.0F;
    float occlusionStrength = 1.0F;
    float emissiveStrength = 1.0F;
    float alphaCutoff = 0.5F;
    float clearcoatFactor = 0.0F;
    float clearcoatRoughnessFactor = 0.0F;
    float sheenColor[3]{ 0.0F, 0.0F, 0.0F };
    float sheenRoughnessFactor = 0.0F;
    float transmissionFactor = 0.0F;
    float thicknessFactor = 0.0F;
    float attenuationColor[3]{ 1.0F, 1.0F, 1.0F };
    float attenuationDistance = 0.0F;
    float subsurfaceColor[3]{ 1.0F, 1.0F, 1.0F };
    float subsurfaceFactor = 0.0F;
    float anisotropyStrength = 0.0F;
    float anisotropyRotation = 0.0F;
    float layerWeight = 1.0F;
    RenderMaterialAlphaMode alphaMode = RenderMaterialAlphaMode::Opaque;
    RenderMaterialDecalBlendMode decalBlendMode = RenderMaterialDecalBlendMode::Disabled;
    RenderMaterialLayerBlendMode layerBlendMode = RenderMaterialLayerBlendMode::Replace;
    bool doubleSided = false;
    std::uint64_t albedoTextureAssetId = 0;
    std::uint64_t normalTextureAssetId = 0;
    std::uint64_t metallicRoughnessTextureAssetId = 0;
    std::uint64_t occlusionTextureAssetId = 0;
    std::uint64_t emissiveTextureAssetId = 0;
    std::uint64_t clearcoatTextureAssetId = 0;
    std::uint64_t clearcoatRoughnessTextureAssetId = 0;
    std::uint64_t sheenColorTextureAssetId = 0;
    std::uint64_t transmissionTextureAssetId = 0;
    std::uint64_t thicknessTextureAssetId = 0;
    std::uint64_t anisotropyTextureAssetId = 0;
    std::uint64_t decalTextureAssetId = 0;
    std::uint64_t layerMaskTextureAssetId = 0;
    RenderTextureHandle albedoTexture{};
    RenderTextureHandle normalTexture{};
    RenderTextureHandle metallicRoughnessTexture{};
    RenderTextureHandle occlusionTexture{};
    RenderTextureHandle emissiveTexture{};
    RenderTextureHandle clearcoatTexture{};
    RenderTextureHandle clearcoatRoughnessTexture{};
    RenderTextureHandle sheenColorTexture{};
    RenderTextureHandle transmissionTexture{};
    RenderTextureHandle thicknessTexture{};
    RenderTextureHandle anisotropyTexture{};
    RenderTextureHandle decalTexture{};
    RenderTextureHandle layerMaskTexture{};
};

struct RenderMaterialResource {
    float baseColor[4]{ 1.0F, 1.0F, 1.0F, 1.0F };
    float emissiveColor[3]{ 0.0F, 0.0F, 0.0F };
    float metallicFactor = 0.0F;
    float roughnessFactor = 1.0F;
    float normalScale = 1.0F;
    float occlusionStrength = 1.0F;
    float emissiveStrength = 1.0F;
    float alphaCutoff = 0.5F;
    float clearcoatFactor = 0.0F;
    float clearcoatRoughnessFactor = 0.0F;
    float sheenColor[3]{ 0.0F, 0.0F, 0.0F };
    float sheenRoughnessFactor = 0.0F;
    float transmissionFactor = 0.0F;
    float thicknessFactor = 0.0F;
    float attenuationColor[3]{ 1.0F, 1.0F, 1.0F };
    float attenuationDistance = 0.0F;
    float subsurfaceColor[3]{ 1.0F, 1.0F, 1.0F };
    float subsurfaceFactor = 0.0F;
    float anisotropyStrength = 0.0F;
    float anisotropyRotation = 0.0F;
    float layerWeight = 1.0F;
    RenderMaterialAlphaMode alphaMode = RenderMaterialAlphaMode::Opaque;
    RenderMaterialDecalBlendMode decalBlendMode = RenderMaterialDecalBlendMode::Disabled;
    RenderMaterialLayerBlendMode layerBlendMode = RenderMaterialLayerBlendMode::Replace;
    bool doubleSided = false;
    std::uint64_t albedoTextureAssetId = 0;
    std::uint64_t normalTextureAssetId = 0;
    std::uint64_t metallicRoughnessTextureAssetId = 0;
    std::uint64_t occlusionTextureAssetId = 0;
    std::uint64_t emissiveTextureAssetId = 0;
    std::uint64_t clearcoatTextureAssetId = 0;
    std::uint64_t clearcoatRoughnessTextureAssetId = 0;
    std::uint64_t sheenColorTextureAssetId = 0;
    std::uint64_t transmissionTextureAssetId = 0;
    std::uint64_t thicknessTextureAssetId = 0;
    std::uint64_t anisotropyTextureAssetId = 0;
    std::uint64_t decalTextureAssetId = 0;
    std::uint64_t layerMaskTextureAssetId = 0;
    RenderTextureHandle albedoTexture{};
    RenderTextureHandle normalTexture{};
    RenderTextureHandle metallicRoughnessTexture{};
    RenderTextureHandle occlusionTexture{};
    RenderTextureHandle emissiveTexture{};
    RenderTextureHandle clearcoatTexture{};
    RenderTextureHandle clearcoatRoughnessTexture{};
    RenderTextureHandle sheenColorTexture{};
    RenderTextureHandle transmissionTexture{};
    RenderTextureHandle thicknessTexture{};
    RenderTextureHandle anisotropyTexture{};
    RenderTextureHandle decalTexture{};
    RenderTextureHandle layerMaskTexture{};
};

struct RenderTextureDesc {
    std::uint16_t width = 0;
    std::uint16_t height = 0;
    bgfx::TextureFormat::Enum format = bgfx::TextureFormat::Count;
    std::uint64_t flags = 0;
    const bgfx::Memory* memory = nullptr;
};

struct RenderTextureResource {
    bgfx::TextureHandle texture = BGFX_INVALID_HANDLE;
    std::uint16_t width = 0;
    std::uint16_t height = 0;
    bgfx::TextureFormat::Enum format = bgfx::TextureFormat::Count;
};

} // namespace kb::render
