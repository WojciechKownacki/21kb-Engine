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
};

struct RenderMeshSection {
    std::uint32_t indexStart = 0;
    std::uint32_t indexCount = 0;
    std::uint32_t materialSlot = 0;
    RenderBoundsSphere bounds{};
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
    RenderBoundsSphere bounds{};
    std::uint64_t rasterStateExtra = 0;
    bool doubleSided = false;
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
    RenderMaterialAlphaMode alphaMode = RenderMaterialAlphaMode::Opaque;
    bool doubleSided = false;
    std::uint64_t albedoTextureAssetId = 0;
    std::uint64_t normalTextureAssetId = 0;
    std::uint64_t metallicRoughnessTextureAssetId = 0;
    std::uint64_t occlusionTextureAssetId = 0;
    std::uint64_t emissiveTextureAssetId = 0;
    RenderTextureHandle albedoTexture{};
    RenderTextureHandle normalTexture{};
    RenderTextureHandle metallicRoughnessTexture{};
    RenderTextureHandle occlusionTexture{};
    RenderTextureHandle emissiveTexture{};
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
    RenderMaterialAlphaMode alphaMode = RenderMaterialAlphaMode::Opaque;
    bool doubleSided = false;
    std::uint64_t albedoTextureAssetId = 0;
    std::uint64_t normalTextureAssetId = 0;
    std::uint64_t metallicRoughnessTextureAssetId = 0;
    std::uint64_t occlusionTextureAssetId = 0;
    std::uint64_t emissiveTextureAssetId = 0;
    RenderTextureHandle albedoTexture{};
    RenderTextureHandle normalTexture{};
    RenderTextureHandle metallicRoughnessTexture{};
    RenderTextureHandle occlusionTexture{};
    RenderTextureHandle emissiveTexture{};
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
