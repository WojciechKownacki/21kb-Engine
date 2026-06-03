#include "kb/render/resources/RenderResourceRegistry.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <utility>

namespace kb::render {
namespace {

constexpr std::uint64_t kDeferredDestroyFrameDelay = 3U;

[[nodiscard]] std::uint32_t NextGeneration(std::uint32_t generation) noexcept {
    const std::uint32_t next = generation + 1U;
    return next == 0U ? 1U : next;
}

[[nodiscard]] std::uint32_t IndexAt(const RenderMeshDesc& desc, std::uint32_t index) noexcept;

[[nodiscard]] bool IsSupportedStaticMeshVertexFormat(RenderVertexFormat format) noexcept {
    switch (format) {
    case RenderVertexFormat::P3C3:
    case RenderVertexFormat::P3N3UV2:
    case RenderVertexFormat::P3N3T4UV2:
        return true;
    case RenderVertexFormat::SkinnedP3N3T4UV2J4W4:
        return false;
    }
    return false;
}

[[nodiscard]] bool IsValidMeshDesc(const RenderMeshDesc& desc) noexcept {
    const void* vertexData = desc.vertexData != nullptr ? desc.vertexData : desc.vertices;
    if (vertexData == nullptr || desc.vertexCount == 0U || desc.indexCount == 0U) {
        return false;
    }
    if (!IsSupportedStaticMeshVertexFormat(desc.vertexFormat) || RenderStaticMeshVertexStride(desc.vertexFormat) == 0U) {
        return false;
    }
    if (desc.indexFormat == RenderIndexFormat::Uint16 && desc.indices == nullptr) {
        return false;
    }
    if (desc.indexFormat == RenderIndexFormat::Uint32 && desc.indices32 == nullptr) {
        return false;
    }
    if (desc.sectionCount > 0U && desc.sections == nullptr) {
        return false;
    }
    for (std::uint32_t sectionIndex = 0; sectionIndex < desc.sectionCount; ++sectionIndex) {
        const RenderMeshSectionDesc& section = desc.sections[sectionIndex];
        if (section.indexCount == 0U || section.indexStart >= desc.indexCount || section.indexCount > desc.indexCount - section.indexStart) {
            return false;
        }
    }
    if (desc.gpuDriven.meshletCount > 0U && desc.gpuDriven.meshlets == nullptr) {
        return false;
    }
    if (desc.gpuDriven.lodCount > 0U && desc.gpuDriven.lods == nullptr) {
        return false;
    }
    for (std::uint32_t meshletIndex = 0U; meshletIndex < desc.gpuDriven.meshletCount; ++meshletIndex) {
        const RenderMeshletDesc& meshlet = desc.gpuDriven.meshlets[meshletIndex];
        if (!meshlet.IsValid() ||
            meshlet.indexStart >= desc.indexCount ||
            meshlet.indexCount > desc.indexCount - meshlet.indexStart ||
            meshlet.vertexStart >= desc.vertexCount ||
            meshlet.vertexCount > desc.vertexCount - meshlet.vertexStart) {
            return false;
        }
    }
    for (std::uint32_t lodIndex = 0U; lodIndex < desc.gpuDriven.lodCount; ++lodIndex) {
        const RenderMeshLodDesc& lod = desc.gpuDriven.lods[lodIndex];
        if (!lod.IsValid() ||
            lod.firstSection > desc.sectionCount ||
            lod.sectionCount > desc.sectionCount - lod.firstSection ||
            lod.firstMeshlet > desc.gpuDriven.meshletCount ||
            lod.meshletCount > desc.gpuDriven.meshletCount - lod.firstMeshlet) {
            return false;
        }
    }
    for (std::uint32_t index = 0U; index < desc.indexCount; ++index) {
        if (IndexAt(desc, index) >= desc.vertexCount) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool IsValidTextureDesc(const RenderTextureDesc& desc) noexcept {
    return desc.width > 0U && desc.height > 0U && desc.format != bgfx::TextureFormat::Count;
}

[[nodiscard]] std::uint32_t IndexAt(const RenderMeshDesc& desc, std::uint32_t index) noexcept {
    return desc.indexFormat == RenderIndexFormat::Uint32 ? desc.indices32[index] : desc.indices[index];
}

[[nodiscard]] std::array<float, 3> VertexPosition(const RenderMeshDesc& desc, std::uint32_t vertexIndex) noexcept {
    const void* vertexData = desc.vertexData != nullptr ? desc.vertexData : desc.vertices;
    switch (desc.vertexFormat) {
    case RenderVertexFormat::P3C3: {
        const auto* vertices = static_cast<const RenderStaticMeshVertex*>(vertexData);
        return { vertices[vertexIndex].x, vertices[vertexIndex].y, vertices[vertexIndex].z };
    }
    case RenderVertexFormat::P3N3UV2: {
        const auto* vertices = static_cast<const RenderStaticMeshVertexP3N3UV2*>(vertexData);
        return { vertices[vertexIndex].x, vertices[vertexIndex].y, vertices[vertexIndex].z };
    }
    case RenderVertexFormat::P3N3T4UV2: {
        const auto* vertices = static_cast<const RenderStaticMeshVertexP3N3T4UV2*>(vertexData);
        return { vertices[vertexIndex].x, vertices[vertexIndex].y, vertices[vertexIndex].z };
    }
    case RenderVertexFormat::SkinnedP3N3T4UV2J4W4: {
        const auto* vertices = static_cast<const RenderStaticMeshVertexSkinned*>(vertexData);
        return { vertices[vertexIndex].x, vertices[vertexIndex].y, vertices[vertexIndex].z };
    }
    }

    return {};
}

[[nodiscard]] RenderBoundsSphere ComputeBounds(const RenderMeshDesc& desc, std::uint32_t indexStart, std::uint32_t indexCount) noexcept {
    if (desc.vertexCount == 0U || indexCount == 0U || indexStart >= desc.indexCount || indexCount > desc.indexCount - indexStart) {
        return {};
    }
    const std::uint32_t indexEnd = indexStart + indexCount;

    const std::uint32_t firstVertexIndex = IndexAt(desc, indexStart);
    if (firstVertexIndex >= desc.vertexCount) {
        return {};
    }

    const std::array<float, 3> first = VertexPosition(desc, firstVertexIndex);
    float minX = first[0];
    float minY = first[1];
    float minZ = first[2];
    float maxX = first[0];
    float maxY = first[1];
    float maxZ = first[2];
    for (std::uint32_t index = indexStart; index < indexEnd; ++index) {
        const std::uint32_t vertexIndex = IndexAt(desc, index);
        if (vertexIndex >= desc.vertexCount) {
            continue;
        }
        const std::array<float, 3> position = VertexPosition(desc, vertexIndex);
        minX = std::min(minX, position[0]);
        minY = std::min(minY, position[1]);
        minZ = std::min(minZ, position[2]);
        maxX = std::max(maxX, position[0]);
        maxY = std::max(maxY, position[1]);
        maxZ = std::max(maxZ, position[2]);
    }

    const std::array<float, 3> center{
        (minX + maxX) * 0.5F,
        (minY + maxY) * 0.5F,
        (minZ + maxZ) * 0.5F,
    };
    float radiusSquared = 0.0F;
    for (std::uint32_t index = indexStart; index < indexEnd; ++index) {
        const std::uint32_t vertexIndex = IndexAt(desc, index);
        if (vertexIndex >= desc.vertexCount) {
            continue;
        }
        const std::array<float, 3> position = VertexPosition(desc, vertexIndex);
        const float dx = position[0] - center[0];
        const float dy = position[1] - center[1];
        const float dz = position[2] - center[2];
        radiusSquared = std::max(radiusSquared, dx * dx + dy * dy + dz * dz);
    }

    return RenderBoundsSphere{
        .center = center,
        .radius = std::sqrt(radiusSquared),
    };
}

[[nodiscard]] std::vector<RenderMeshSection> BuildSections(const RenderMeshDesc& desc, RenderBoundsSphere meshBounds) {
    if (desc.sectionCount == 0U) {
        return std::vector<RenderMeshSection>{
            RenderMeshSection{
                .indexStart = 0U,
                .indexCount = desc.indexCount,
                .materialSlot = 0U,
                .bounds = meshBounds,
            },
        };
    }

    std::vector<RenderMeshSection> sections;
    sections.reserve(desc.sectionCount);
    for (std::uint32_t sectionIndex = 0U; sectionIndex < desc.sectionCount; ++sectionIndex) {
        const RenderMeshSectionDesc& section = desc.sections[sectionIndex];
        sections.push_back(RenderMeshSection{
            .indexStart = section.indexStart,
            .indexCount = section.indexCount,
            .materialSlot = section.materialSlot,
            .bounds = section.bounds.IsValid() ? section.bounds : ComputeBounds(desc, section.indexStart, section.indexCount),
            .lodLevel = section.lodLevel,
        });
    }
    return sections;
}

[[nodiscard]] std::vector<RenderMaterialSlot> BuildMaterialSlots(const RenderMeshDesc& desc, const std::vector<RenderMeshSection>& sections) {
    std::uint32_t requiredSlots = desc.materialSlotCount;
    for (const RenderMeshSection& section : sections) {
        requiredSlots = std::max(requiredSlots, section.materialSlot + 1U);
    }
    requiredSlots = std::max(requiredSlots, 1U);

    std::vector<RenderMaterialSlot> slots;
    slots.reserve(requiredSlots);
    for (std::uint32_t slotIndex = 0U; slotIndex < requiredSlots; ++slotIndex) {
        const std::uint64_t defaultMaterial = desc.materialSlots != nullptr && slotIndex < desc.materialSlotCount
            ? desc.materialSlots[slotIndex].defaultMaterialAssetId
            : 0U;
        slots.push_back(RenderMaterialSlot{
            .defaultMaterialAssetId = defaultMaterial,
        });
    }
    return slots;
}

} // namespace

bgfx::VertexLayout RenderStaticMeshVertexLayout() {
    return RenderStaticMeshVertexLayout(RenderVertexFormat::P3C3);
}

bgfx::VertexLayout RenderStaticMeshVertexLayout(RenderVertexFormat format) {
    bgfx::VertexLayout layout;
    layout.begin();
    switch (format) {
    case RenderVertexFormat::P3C3:
        layout
            .add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float)
            .add(bgfx::Attrib::Color0, 3, bgfx::AttribType::Float);
        break;
    case RenderVertexFormat::P3N3UV2:
        layout
            .add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float)
            .add(bgfx::Attrib::Normal, 3, bgfx::AttribType::Float)
            .add(bgfx::Attrib::TexCoord0, 2, bgfx::AttribType::Float)
            .add(bgfx::Attrib::Color0, 3, bgfx::AttribType::Float);
        break;
    case RenderVertexFormat::P3N3T4UV2:
        layout
            .add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float)
            .add(bgfx::Attrib::Normal, 3, bgfx::AttribType::Float)
            .add(bgfx::Attrib::Tangent, 4, bgfx::AttribType::Float)
            .add(bgfx::Attrib::TexCoord0, 2, bgfx::AttribType::Float)
            .add(bgfx::Attrib::Color0, 3, bgfx::AttribType::Float);
        break;
    case RenderVertexFormat::SkinnedP3N3T4UV2J4W4:
        layout
            .add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float)
            .add(bgfx::Attrib::Normal, 3, bgfx::AttribType::Float)
            .add(bgfx::Attrib::Tangent, 4, bgfx::AttribType::Float)
            .add(bgfx::Attrib::TexCoord0, 2, bgfx::AttribType::Float)
            .add(bgfx::Attrib::Color0, 3, bgfx::AttribType::Float)
            .add(bgfx::Attrib::Indices, 4, bgfx::AttribType::Uint16)
            .add(bgfx::Attrib::Weight, 4, bgfx::AttribType::Float);
        break;
    }
    layout.end();
    return layout;
}

std::uint32_t RenderStaticMeshVertexStride(RenderVertexFormat format) noexcept {
    switch (format) {
    case RenderVertexFormat::P3C3:
        return sizeof(RenderStaticMeshVertex);
    case RenderVertexFormat::P3N3UV2:
        return sizeof(RenderStaticMeshVertexP3N3UV2);
    case RenderVertexFormat::P3N3T4UV2:
        return sizeof(RenderStaticMeshVertexP3N3T4UV2);
    case RenderVertexFormat::SkinnedP3N3T4UV2J4W4:
        return sizeof(RenderStaticMeshVertexSkinned);
    }
    return 0U;
}

RenderResourceRegistry::RenderResourceRegistry() {
    meshes_.push_back(MeshSlot{});
    materials_.push_back(MaterialSlot{});
    textures_.push_back(TextureSlot{});
}

RenderResourceRegistry::~RenderResourceRegistry() {
    Shutdown();
}

RenderMeshHandle RenderResourceRegistry::RegisterMesh(const RenderMeshDesc& desc) {
    if (!IsValidMeshDesc(desc)) {
        return {};
    }

    const bgfx::VertexLayout layout = RenderStaticMeshVertexLayout(desc.vertexFormat);
    const void* vertexData = desc.vertexData != nullptr ? desc.vertexData : desc.vertices;
    const bgfx::Memory* vertexMemory = bgfx::copy(vertexData, static_cast<std::uint32_t>(RenderStaticMeshVertexStride(desc.vertexFormat) * desc.vertexCount));
    bgfx::VertexBufferHandle vertexBuffer = bgfx::createVertexBuffer(vertexMemory, layout);
    if (!bgfx::isValid(vertexBuffer)) {
        return {};
    }

    const bgfx::Memory* indexMemory = desc.indexFormat == RenderIndexFormat::Uint32
        ? bgfx::copy(desc.indices32, static_cast<std::uint32_t>(sizeof(std::uint32_t) * desc.indexCount))
        : bgfx::copy(desc.indices, static_cast<std::uint32_t>(sizeof(std::uint16_t) * desc.indexCount));
    const std::uint16_t indexFlags = desc.indexFormat == RenderIndexFormat::Uint32 ? BGFX_BUFFER_INDEX32 : 0U;
    bgfx::IndexBufferHandle indexBuffer = bgfx::createIndexBuffer(indexMemory, indexFlags);
    if (!bgfx::isValid(indexBuffer)) {
        bgfx::destroy(vertexBuffer);
        return {};
    }

    const std::uint32_t slotIndex = AllocateMeshSlot();
    MeshSlot& slot = meshes_[slotIndex];
    const RenderBoundsSphere meshBounds = desc.bounds.IsValid() ? desc.bounds : ComputeBounds(desc, 0U, desc.indexCount);
    std::vector<RenderMeshSection> sections = BuildSections(desc, meshBounds);
    std::vector<RenderMaterialSlot> materialSlots = BuildMaterialSlots(desc, sections);
    std::vector<RenderMeshletDesc> meshlets;
    if (desc.gpuDriven.meshletCount > 0U) {
        meshlets.assign(desc.gpuDriven.meshlets, desc.gpuDriven.meshlets + desc.gpuDriven.meshletCount);
    }
    std::vector<RenderMeshLodDesc> lods;
    if (desc.gpuDriven.lodCount > 0U) {
        lods.assign(desc.gpuDriven.lods, desc.gpuDriven.lods + desc.gpuDriven.lodCount);
    }
    slot.resource = RenderMeshResource{
        .vertexBuffer = vertexBuffer,
        .indexBuffer = indexBuffer,
        .vertexCount = desc.vertexCount,
        .indexCount = desc.indexCount,
        .vertexFormat = desc.vertexFormat,
        .indexFormat = desc.indexFormat,
        .sections = std::move(sections),
        .materialSlots = std::move(materialSlots),
        .meshlets = std::move(meshlets),
        .lods = std::move(lods),
        .bounds = meshBounds,
        .rasterStateExtra = desc.rasterStateExtra,
        .doubleSided = desc.doubleSided,
        .gpuCullingEnabled = desc.gpuDriven.allowGpuCulling && desc.gpuDriven.meshletCount > 0U,
        .indirectDrawsEnabled = desc.gpuDriven.allowIndirectDraws && desc.gpuDriven.meshletCount > 0U,
        .meshletCullingEnabled = desc.gpuDriven.allowMeshletCulling && desc.gpuDriven.meshletCount > 0U,
    };
    slot.occupied = true;
    slot.pendingDestroy = false;
    return RenderMeshHandle{ detail::MakeRenderHandleValue(slotIndex, slot.generation) };
}

const RenderMeshResource* RenderResourceRegistry::FindMesh(RenderMeshHandle handle) const noexcept {
    const std::uint32_t index = handle.Index();
    if (!handle.IsValid() || index == 0U || index >= meshes_.size()) {
        return nullptr;
    }

    const MeshSlot& slot = meshes_[index];
    if (!slot.occupied || slot.pendingDestroy || slot.generation != handle.Generation()) {
        return nullptr;
    }
    return &slot.resource;
}

bool RenderResourceRegistry::ContainsMesh(RenderMeshHandle handle) const noexcept {
    return FindMesh(handle) != nullptr;
}

void RenderResourceRegistry::DestroyMesh(RenderMeshHandle handle) noexcept {
    const std::uint32_t index = handle.Index();
    if (!handle.IsValid() || index == 0U || index >= meshes_.size()) {
        return;
    }

    MeshSlot& slot = meshes_[index];
    if (!slot.occupied || slot.pendingDestroy || slot.generation != handle.Generation()) {
        return;
    }

    slot.occupied = false;
    slot.pendingDestroy = true;
    QueueDestroy(DeferredDestroyKind::Mesh, index);
}

RenderMaterialHandle RenderResourceRegistry::RegisterMaterial(const RenderMaterialDesc& desc) {
    const std::uint32_t slotIndex = AllocateMaterialSlot();
    MaterialSlot& slot = materials_[slotIndex];
    std::memcpy(slot.resource.baseColor, desc.baseColor, sizeof(slot.resource.baseColor));
    std::memcpy(slot.resource.emissiveColor, desc.emissiveColor, sizeof(slot.resource.emissiveColor));
    slot.resource.metallicFactor = desc.metallicFactor;
    slot.resource.roughnessFactor = desc.roughnessFactor;
    slot.resource.normalScale = desc.normalScale;
    slot.resource.occlusionStrength = desc.occlusionStrength;
    slot.resource.emissiveStrength = desc.emissiveStrength;
    slot.resource.alphaCutoff = desc.alphaCutoff;
    slot.resource.clearcoatFactor = desc.clearcoatFactor;
    slot.resource.clearcoatRoughnessFactor = desc.clearcoatRoughnessFactor;
    std::memcpy(slot.resource.sheenColor, desc.sheenColor, sizeof(slot.resource.sheenColor));
    slot.resource.sheenRoughnessFactor = desc.sheenRoughnessFactor;
    slot.resource.transmissionFactor = desc.transmissionFactor;
    slot.resource.thicknessFactor = desc.thicknessFactor;
    std::memcpy(slot.resource.attenuationColor, desc.attenuationColor, sizeof(slot.resource.attenuationColor));
    slot.resource.attenuationDistance = desc.attenuationDistance;
    std::memcpy(slot.resource.subsurfaceColor, desc.subsurfaceColor, sizeof(slot.resource.subsurfaceColor));
    slot.resource.subsurfaceFactor = desc.subsurfaceFactor;
    slot.resource.anisotropyStrength = desc.anisotropyStrength;
    slot.resource.anisotropyRotation = desc.anisotropyRotation;
    slot.resource.layerWeight = desc.layerWeight;
    slot.resource.alphaMode = desc.alphaMode;
    slot.resource.decalBlendMode = desc.decalBlendMode;
    slot.resource.layerBlendMode = desc.layerBlendMode;
    slot.resource.doubleSided = desc.doubleSided;
    slot.resource.albedoTextureAssetId = desc.albedoTextureAssetId;
    slot.resource.normalTextureAssetId = desc.normalTextureAssetId;
    slot.resource.metallicRoughnessTextureAssetId = desc.metallicRoughnessTextureAssetId;
    slot.resource.occlusionTextureAssetId = desc.occlusionTextureAssetId;
    slot.resource.emissiveTextureAssetId = desc.emissiveTextureAssetId;
    slot.resource.clearcoatTextureAssetId = desc.clearcoatTextureAssetId;
    slot.resource.clearcoatRoughnessTextureAssetId = desc.clearcoatRoughnessTextureAssetId;
    slot.resource.sheenColorTextureAssetId = desc.sheenColorTextureAssetId;
    slot.resource.transmissionTextureAssetId = desc.transmissionTextureAssetId;
    slot.resource.thicknessTextureAssetId = desc.thicknessTextureAssetId;
    slot.resource.anisotropyTextureAssetId = desc.anisotropyTextureAssetId;
    slot.resource.decalTextureAssetId = desc.decalTextureAssetId;
    slot.resource.layerMaskTextureAssetId = desc.layerMaskTextureAssetId;
    slot.resource.albedoTexture = desc.albedoTexture;
    slot.resource.normalTexture = desc.normalTexture;
    slot.resource.metallicRoughnessTexture = desc.metallicRoughnessTexture;
    slot.resource.occlusionTexture = desc.occlusionTexture;
    slot.resource.emissiveTexture = desc.emissiveTexture;
    slot.resource.clearcoatTexture = desc.clearcoatTexture;
    slot.resource.clearcoatRoughnessTexture = desc.clearcoatRoughnessTexture;
    slot.resource.sheenColorTexture = desc.sheenColorTexture;
    slot.resource.transmissionTexture = desc.transmissionTexture;
    slot.resource.thicknessTexture = desc.thicknessTexture;
    slot.resource.anisotropyTexture = desc.anisotropyTexture;
    slot.resource.decalTexture = desc.decalTexture;
    slot.resource.layerMaskTexture = desc.layerMaskTexture;
    slot.occupied = true;
    slot.pendingDestroy = false;
    return RenderMaterialHandle{ detail::MakeRenderHandleValue(slotIndex, slot.generation) };
}

const RenderMaterialResource* RenderResourceRegistry::FindMaterial(RenderMaterialHandle handle) const noexcept {
    const std::uint32_t index = handle.Index();
    if (!handle.IsValid() || index == 0U || index >= materials_.size()) {
        return nullptr;
    }

    const MaterialSlot& slot = materials_[index];
    if (!slot.occupied || slot.pendingDestroy || slot.generation != handle.Generation()) {
        return nullptr;
    }
    return &slot.resource;
}

bool RenderResourceRegistry::ContainsMaterial(RenderMaterialHandle handle) const noexcept {
    return FindMaterial(handle) != nullptr;
}

void RenderResourceRegistry::DestroyMaterial(RenderMaterialHandle handle) noexcept {
    const std::uint32_t index = handle.Index();
    if (!handle.IsValid() || index == 0U || index >= materials_.size()) {
        return;
    }

    MaterialSlot& slot = materials_[index];
    if (!slot.occupied || slot.pendingDestroy || slot.generation != handle.Generation()) {
        return;
    }

    slot.occupied = false;
    slot.pendingDestroy = true;
    QueueDestroy(DeferredDestroyKind::Material, index);
}

RenderTextureHandle RenderResourceRegistry::RegisterTexture2D(const RenderTextureDesc& desc) {
    if (!IsValidTextureDesc(desc)) {
        return {};
    }

    bgfx::TextureHandle texture = bgfx::createTexture2D(desc.width, desc.height, false, 1, desc.format, desc.flags, desc.memory);
    if (!bgfx::isValid(texture)) {
        return {};
    }

    const std::uint32_t slotIndex = AllocateTextureSlot();
    TextureSlot& slot = textures_[slotIndex];
    slot.resource = RenderTextureResource{
        .texture = texture,
        .width = desc.width,
        .height = desc.height,
        .format = desc.format,
    };
    slot.occupied = true;
    slot.pendingDestroy = false;
    return RenderTextureHandle{ detail::MakeRenderHandleValue(slotIndex, slot.generation) };
}

const RenderTextureResource* RenderResourceRegistry::FindTexture(RenderTextureHandle handle) const noexcept {
    const std::uint32_t index = handle.Index();
    if (!handle.IsValid() || index == 0U || index >= textures_.size()) {
        return nullptr;
    }

    const TextureSlot& slot = textures_[index];
    if (!slot.occupied || slot.pendingDestroy || slot.generation != handle.Generation()) {
        return nullptr;
    }
    return &slot.resource;
}

bool RenderResourceRegistry::ContainsTexture(RenderTextureHandle handle) const noexcept {
    return FindTexture(handle) != nullptr;
}

void RenderResourceRegistry::DestroyTexture(RenderTextureHandle handle) noexcept {
    const std::uint32_t index = handle.Index();
    if (!handle.IsValid() || index == 0U || index >= textures_.size()) {
        return;
    }

    TextureSlot& slot = textures_[index];
    if (!slot.occupied || slot.pendingDestroy || slot.generation != handle.Generation()) {
        return;
    }

    slot.occupied = false;
    slot.pendingDestroy = true;
    QueueDestroy(DeferredDestroyKind::Texture, index);
}

void RenderResourceRegistry::Reserve(const RenderResourceRegistryReserveDesc& desc) {
    if (desc.meshSlots > 0U) {
        meshes_.reserve(static_cast<std::size_t>(desc.meshSlots) + 1U);
        freeMeshSlots_.reserve(desc.meshSlots);
    }
    if (desc.materialSlots > 0U) {
        materials_.reserve(static_cast<std::size_t>(desc.materialSlots) + 1U);
        freeMaterialSlots_.reserve(desc.materialSlots);
    }
    if (desc.textureSlots > 0U) {
        textures_.reserve(static_cast<std::size_t>(desc.textureSlots) + 1U);
        freeTextureSlots_.reserve(desc.textureSlots);
    }
}

void RenderResourceRegistry::TickFrame() noexcept {
    ++frameNumber_;
    std::size_t writeIndex = 0;
    for (std::size_t readIndex = 0; readIndex < deferredDestroy_.size(); ++readIndex) {
        DeferredDestroyEntry entry = deferredDestroy_[readIndex];
        if (entry.releaseFrame > frameNumber_) {
            deferredDestroy_[writeIndex++] = entry;
            continue;
        }
        ReleaseDeferred(entry);
    }
    deferredDestroy_.resize(writeIndex);
}

void RenderResourceRegistry::Shutdown() noexcept {
    deferredDestroy_.clear();
    freeMeshSlots_.clear();
    freeMaterialSlots_.clear();
    freeTextureSlots_.clear();

    for (std::uint32_t index = 1U; index < meshes_.size(); ++index) {
        ReleaseMeshResource(meshes_[index].resource);
        meshes_[index].occupied = false;
        meshes_[index].pendingDestroy = false;
        meshes_[index].generation = NextGeneration(meshes_[index].generation);
        freeMeshSlots_.push_back(index);
    }
    for (std::uint32_t index = 1U; index < materials_.size(); ++index) {
        materials_[index].resource = RenderMaterialResource{};
        materials_[index].occupied = false;
        materials_[index].pendingDestroy = false;
        materials_[index].generation = NextGeneration(materials_[index].generation);
        freeMaterialSlots_.push_back(index);
    }
    for (std::uint32_t index = 1U; index < textures_.size(); ++index) {
        ReleaseTextureResource(textures_[index].resource);
        textures_[index].occupied = false;
        textures_[index].pendingDestroy = false;
        textures_[index].generation = NextGeneration(textures_[index].generation);
        freeTextureSlots_.push_back(index);
    }
}

RenderResourceRegistryStats RenderResourceRegistry::Stats() const noexcept {
    RenderResourceRegistryStats stats{};
    stats.frameNumber = frameNumber_;
    for (std::uint32_t index = 1U; index < meshes_.size(); ++index) {
        stats.meshCount += meshes_[index].occupied ? 1U : 0U;
    }
    for (std::uint32_t index = 1U; index < materials_.size(); ++index) {
        stats.materialCount += materials_[index].occupied ? 1U : 0U;
    }
    for (std::uint32_t index = 1U; index < textures_.size(); ++index) {
        stats.textureCount += textures_[index].occupied ? 1U : 0U;
    }
    stats.pendingDestroyCount = static_cast<std::uint32_t>(deferredDestroy_.size());
    for (const DeferredDestroyEntry& entry : deferredDestroy_) {
        switch (entry.kind) {
        case DeferredDestroyKind::Mesh:
            ++stats.pendingMeshDestroyCount;
            break;
        case DeferredDestroyKind::Material:
            ++stats.pendingMaterialDestroyCount;
            break;
        case DeferredDestroyKind::Texture:
            ++stats.pendingTextureDestroyCount;
            break;
        }
    }
    stats.meshSlotCapacity = meshes_.capacity() == 0U ? 0U : static_cast<std::uint32_t>(meshes_.capacity() - 1U);
    stats.materialSlotCapacity = materials_.capacity() == 0U ? 0U : static_cast<std::uint32_t>(materials_.capacity() - 1U);
    stats.textureSlotCapacity = textures_.capacity() == 0U ? 0U : static_cast<std::uint32_t>(textures_.capacity() - 1U);
    stats.freeMeshSlotCount = static_cast<std::uint32_t>(freeMeshSlots_.size());
    stats.freeMaterialSlotCount = static_cast<std::uint32_t>(freeMaterialSlots_.size());
    stats.freeTextureSlotCount = static_cast<std::uint32_t>(freeTextureSlots_.size());
    return stats;
}

std::uint32_t RenderResourceRegistry::AllocateMeshSlot() {
    if (!freeMeshSlots_.empty()) {
        const std::uint32_t index = freeMeshSlots_.back();
        freeMeshSlots_.pop_back();
        return index;
    }
    meshes_.push_back(MeshSlot{});
    return static_cast<std::uint32_t>(meshes_.size() - 1U);
}

std::uint32_t RenderResourceRegistry::AllocateMaterialSlot() {
    if (!freeMaterialSlots_.empty()) {
        const std::uint32_t index = freeMaterialSlots_.back();
        freeMaterialSlots_.pop_back();
        return index;
    }
    materials_.push_back(MaterialSlot{});
    return static_cast<std::uint32_t>(materials_.size() - 1U);
}

std::uint32_t RenderResourceRegistry::AllocateTextureSlot() {
    if (!freeTextureSlots_.empty()) {
        const std::uint32_t index = freeTextureSlots_.back();
        freeTextureSlots_.pop_back();
        return index;
    }
    textures_.push_back(TextureSlot{});
    return static_cast<std::uint32_t>(textures_.size() - 1U);
}

void RenderResourceRegistry::QueueDestroy(DeferredDestroyKind kind, std::uint32_t slot) noexcept {
    deferredDestroy_.push_back(DeferredDestroyEntry{
        .kind = kind,
        .slot = slot,
        .releaseFrame = frameNumber_ + kDeferredDestroyFrameDelay,
    });
}

void RenderResourceRegistry::ReleaseDeferred(const DeferredDestroyEntry& entry) noexcept {
    switch (entry.kind) {
    case DeferredDestroyKind::Mesh:
        if (entry.slot < meshes_.size() && meshes_[entry.slot].pendingDestroy) {
            ReleaseMeshResource(meshes_[entry.slot].resource);
            meshes_[entry.slot].generation = NextGeneration(meshes_[entry.slot].generation);
            meshes_[entry.slot].pendingDestroy = false;
            freeMeshSlots_.push_back(entry.slot);
        }
        break;
    case DeferredDestroyKind::Material:
        if (entry.slot < materials_.size() && materials_[entry.slot].pendingDestroy) {
            materials_[entry.slot].resource = RenderMaterialResource{};
            materials_[entry.slot].generation = NextGeneration(materials_[entry.slot].generation);
            materials_[entry.slot].pendingDestroy = false;
            freeMaterialSlots_.push_back(entry.slot);
        }
        break;
    case DeferredDestroyKind::Texture:
        if (entry.slot < textures_.size() && textures_[entry.slot].pendingDestroy) {
            ReleaseTextureResource(textures_[entry.slot].resource);
            textures_[entry.slot].generation = NextGeneration(textures_[entry.slot].generation);
            textures_[entry.slot].pendingDestroy = false;
            freeTextureSlots_.push_back(entry.slot);
        }
        break;
    }
}

void RenderResourceRegistry::ReleaseMeshResource(RenderMeshResource& resource) noexcept {
    if (bgfx::isValid(resource.indexBuffer)) {
        bgfx::destroy(resource.indexBuffer);
    }
    if (bgfx::isValid(resource.vertexBuffer)) {
        bgfx::destroy(resource.vertexBuffer);
    }
    resource = RenderMeshResource{};
}

void RenderResourceRegistry::ReleaseTextureResource(RenderTextureResource& resource) noexcept {
    if (bgfx::isValid(resource.texture)) {
        bgfx::destroy(resource.texture);
    }
    resource = RenderTextureResource{};
}

} // namespace kb::render
