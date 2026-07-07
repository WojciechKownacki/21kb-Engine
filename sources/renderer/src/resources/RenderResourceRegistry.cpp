#include "kb/render/resources/RenderResourceRegistry.hpp"

#include "resources/RenderMaterialResourceBuilder.hpp"
#include "resources/RenderMeshResourceBuilder.hpp"
#include "resources/RenderResourceReleaser.hpp"
#include "resources/RenderTextureResourceBuilder.hpp"
#include "renderer/RendererDebugLog.hpp"

#include <sstream>
#include <utility>

namespace kb::render {
namespace {

constexpr std::uint64_t kDeferredDestroyFrameDelay = 3U;

[[nodiscard]] std::string_view VertexFormatName(RenderVertexFormat format) noexcept {
    switch (format) {
    case RenderVertexFormat::P3C3: return "P3C3";
    case RenderVertexFormat::P3N3UV2: return "P3N3UV2";
    case RenderVertexFormat::P3N3T4UV2: return "P3N3T4UV2";
    case RenderVertexFormat::SkinnedP3N3T4UV2J4W4: return "SkinnedP3N3T4UV2J4W4";
    }
    return "Unknown";
}

[[nodiscard]] bool VertexFormatHasTangent(RenderVertexFormat format) noexcept {
    return format == RenderVertexFormat::P3N3T4UV2 ||
        format == RenderVertexFormat::SkinnedP3N3T4UV2J4W4;
}

[[nodiscard]] std::string_view ResourceColorSpaceName(RenderTextureColorSpace colorSpace) noexcept {
    switch (colorSpace) {
    case RenderTextureColorSpace::Srgb: return "Srgb";
    case RenderTextureColorSpace::Linear: return "Linear";
    }
    return "Linear";
}

} // namespace

RenderResourceRegistry::RenderResourceRegistry() = default;

RenderResourceRegistry::~RenderResourceRegistry() {
    Shutdown();
}

RenderMeshHandle RenderResourceRegistry::RegisterMesh(const RenderMeshDesc& desc) {
    if (!RenderMeshResourceBuilder::IsValidDesc(desc)) {
        WriteRendererMaterialGraphDebugLog("resource", "register-mesh-rejected invalid desc");
        return {};
    }

    const bgfx::VertexLayout layout = RenderStaticMeshVertexLayout(desc.vertexFormat);
    const void* vertexData = RenderMeshResourceBuilder::VertexData(desc);
    {
        std::ostringstream row;
        row << "register-mesh-begin vertices=" << desc.vertexCount
            << " indices=" << desc.indexCount
            << " vertexFormat=" << VertexFormatName(desc.vertexFormat)
            << " hasTangent=" << (VertexFormatHasTangent(desc.vertexFormat) ? "true" : "false")
            << " stride=" << RenderStaticMeshVertexStride(desc.vertexFormat)
            << " sections=" << desc.sectionCount
            << " materialSlots=" << desc.materialSlotCount
            << " doubleSided=" << (desc.doubleSided ? "true" : "false");
        WriteRendererMaterialGraphDebugLog("resource", row.str());
    }
    const bgfx::Memory* vertexMemory = bgfx::copy(vertexData, static_cast<std::uint32_t>(RenderStaticMeshVertexStride(desc.vertexFormat) * desc.vertexCount));
    bgfx::VertexBufferHandle vertexBuffer = bgfx::createVertexBuffer(vertexMemory, layout);
    if (!bgfx::isValid(vertexBuffer)) {
        WriteRendererMaterialGraphDebugLog("resource", "register-mesh-failed vertex buffer invalid");
        return {};
    }

    const bgfx::Memory* indexMemory = desc.indexFormat == RenderIndexFormat::Uint32
        ? bgfx::copy(desc.indices32, static_cast<std::uint32_t>(sizeof(std::uint32_t) * desc.indexCount))
        : bgfx::copy(desc.indices, static_cast<std::uint32_t>(sizeof(std::uint16_t) * desc.indexCount));
    const std::uint16_t indexFlags = desc.indexFormat == RenderIndexFormat::Uint32 ? BGFX_BUFFER_INDEX32 : 0U;
    bgfx::IndexBufferHandle indexBuffer = bgfx::createIndexBuffer(indexMemory, indexFlags);
    if (!bgfx::isValid(indexBuffer)) {
        bgfx::destroy(vertexBuffer);
        WriteRendererMaterialGraphDebugLog("resource", "register-mesh-failed index buffer invalid");
        return {};
    }

    const std::uint32_t slotIndex = meshes_.Allocate();
    RenderMeshResource resource = RenderMeshResourceBuilder::Build(desc, vertexBuffer, indexBuffer);
    resource.version = AllocateResourceVersion();
    meshes_.Activate(slotIndex, std::move(resource));
    {
        const RenderMeshHandle handle{ detail::MakeRenderHandleValue(slotIndex, meshes_.Generation(slotIndex)) };
        std::ostringstream row;
        row << "register-mesh-ok handle=" << handle.value
            << " vb=" << vertexBuffer.idx
            << " ib=" << indexBuffer.idx
            << " vertexFormat=" << VertexFormatName(desc.vertexFormat)
            << " hasTangent=" << (VertexFormatHasTangent(desc.vertexFormat) ? "true" : "false");
        WriteRendererMaterialGraphDebugLog("resource", row.str());
        return handle;
    }
}

const RenderMeshResource* RenderResourceRegistry::FindMesh(RenderMeshHandle handle) const noexcept {
    return meshes_.Find(handle);
}

bool RenderResourceRegistry::ContainsMesh(RenderMeshHandle handle) const noexcept {
    return FindMesh(handle) != nullptr;
}

void RenderResourceRegistry::DestroyMesh(RenderMeshHandle handle) noexcept {
    const std::uint32_t slotIndex = meshes_.MarkPendingDestroy(handle);
    if (slotIndex != 0U) {
        QueueDestroy(DeferredDestroyKind::Mesh, slotIndex);
    }
}

RenderMaterialHandle RenderResourceRegistry::RegisterMaterial(const RenderMaterialDesc& desc) {
    return RegisterMaterial(desc, RenderMaterialGraphProgramBinding{});
}

RenderMaterialHandle RenderResourceRegistry::RegisterMaterial(const RenderMaterialDesc& desc, RenderMaterialGraphProgramBinding graphProgram) {
    const std::uint32_t slotIndex = materials_.Allocate();
    RenderMaterialResource resource = RenderMaterialResourceBuilder::Build(desc);
    resource.graphProgram = std::move(graphProgram);
    if (resource.graphProgram.active) {
        // MAT-38/#25d: a graph material's blend mode (resolved by the binding builder) drives the scene
        // render state, so a translucent graph material is submitted in the transparent pass with the
        // authored blend equation instead of rendering opaque.
        resource.alphaMode = resource.graphProgram.alphaMode;
        resource.translucencyBlend = resource.graphProgram.translucencyBlend;
    }
    resource.version = AllocateResourceVersion();
    materials_.Activate(slotIndex, std::move(resource));
    const RenderMaterialHandle handle{ detail::MakeRenderHandleValue(slotIndex, materials_.Generation(slotIndex)) };
    if (const RenderMaterialResource* material = materials_.Find(handle); material != nullptr && material->graphProgram.active) {
        std::ostringstream row;
        row << "register-material-graph handle=" << handle.value
            << " graphHash=" << material->graphProgram.graphSourceHash
            << " textures=" << material->graphProgram.textures.size()
            << " uniforms=" << material->graphProgram.uniforms.size()
            << " normalTextureAssetId=" << material->normalTextureAssetId
            << " normalScale=" << material->normalScale
            << " alphaMode=" << static_cast<int>(material->alphaMode);
        WriteRendererMaterialGraphDebugLog("resource", row.str());
    }
    return handle;
}

const RenderMaterialResource* RenderResourceRegistry::FindMaterial(RenderMaterialHandle handle) const noexcept {
    return materials_.Find(handle);
}

bool RenderResourceRegistry::ContainsMaterial(RenderMaterialHandle handle) const noexcept {
    return FindMaterial(handle) != nullptr;
}

void RenderResourceRegistry::DestroyMaterial(RenderMaterialHandle handle) noexcept {
    const std::uint32_t slotIndex = materials_.MarkPendingDestroy(handle);
    if (slotIndex != 0U) {
        QueueDestroy(DeferredDestroyKind::Material, slotIndex);
    }
}

RenderTextureHandle RenderResourceRegistry::RegisterTexture2D(const RenderTextureDesc& desc) {
    if (!RenderTextureResourceBuilder::IsValidDesc(desc)) {
        WriteRendererMaterialGraphDebugLog("resource", "register-texture-rejected invalid desc");
        return {};
    }

    bgfx::TextureHandle texture = bgfx::createTexture2D(desc.width, desc.height, false, 1, desc.format, desc.flags, desc.memory);
    if (!bgfx::isValid(texture)) {
        WriteRendererMaterialGraphDebugLog("resource", "register-texture-failed bgfx invalid");
        return {};
    }

    const std::uint32_t slotIndex = textures_.Allocate();
    RenderTextureResource resource = RenderTextureResourceBuilder::Build(desc, texture);
    resource.version = AllocateResourceVersion();
    textures_.Activate(slotIndex, std::move(resource));
    const RenderTextureHandle handle{ detail::MakeRenderHandleValue(slotIndex, textures_.Generation(slotIndex)) };
    {
        std::ostringstream row;
        row << "register-texture-ok handle=" << handle.value
            << " bgfxHandle=" << texture.idx
            << " size=" << desc.width << "x" << desc.height
            << " format=" << static_cast<int>(desc.format)
            << " colorSpace=" << ResourceColorSpaceName(desc.colorSpace)
            << " flags=0x" << std::hex << desc.flags << std::dec;
        WriteRendererMaterialGraphDebugLog("resource", row.str());
    }
    return handle;
}

const RenderTextureResource* RenderResourceRegistry::FindTexture(RenderTextureHandle handle) const noexcept {
    return textures_.Find(handle);
}

bool RenderResourceRegistry::ContainsTexture(RenderTextureHandle handle) const noexcept {
    return FindTexture(handle) != nullptr;
}

void RenderResourceRegistry::DestroyTexture(RenderTextureHandle handle) noexcept {
    const std::uint32_t slotIndex = textures_.MarkPendingDestroy(handle);
    if (slotIndex != 0U) {
        QueueDestroy(DeferredDestroyKind::Texture, slotIndex);
    }
}

void RenderResourceRegistry::Reserve(const RenderResourceRegistryReserveDesc& desc) {
    meshes_.Reserve(desc.meshSlots);
    materials_.Reserve(desc.materialSlots);
    textures_.Reserve(desc.textureSlots);
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
    meshes_.Shutdown([](RenderMeshResource& resource) noexcept {
        RenderResourceReleaser::ReleaseMesh(resource);
    });
    materials_.Shutdown([](RenderMaterialResource& resource) noexcept {
        resource = RenderMaterialResource{};
    });
    textures_.Shutdown([](RenderTextureResource& resource) noexcept {
        RenderResourceReleaser::ReleaseTexture(resource);
    });
}

RenderResourceRegistryStats RenderResourceRegistry::Stats() const noexcept {
    RenderResourceRegistryStats stats{};
    stats.frameNumber = frameNumber_;
    stats.meshCount = meshes_.LiveCount();
    stats.materialCount = materials_.LiveCount();
    stats.textureCount = textures_.LiveCount();
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
    stats.meshSlotCapacity = meshes_.SlotCapacity();
    stats.materialSlotCapacity = materials_.SlotCapacity();
    stats.textureSlotCapacity = textures_.SlotCapacity();
    stats.freeMeshSlotCount = meshes_.FreeSlotCount();
    stats.freeMaterialSlotCount = materials_.FreeSlotCount();
    stats.freeTextureSlotCount = textures_.FreeSlotCount();
    return stats;
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
        meshes_.ReleasePending(entry.slot, [](RenderMeshResource& resource) noexcept {
            RenderResourceReleaser::ReleaseMesh(resource);
        });
        break;
    case DeferredDestroyKind::Material:
        materials_.ReleasePending(entry.slot, [](RenderMaterialResource& resource) noexcept {
            resource = RenderMaterialResource{};
        });
        break;
    case DeferredDestroyKind::Texture:
        textures_.ReleasePending(entry.slot, [](RenderTextureResource& resource) noexcept {
            RenderResourceReleaser::ReleaseTexture(resource);
        });
        break;
    }
}

std::uint64_t RenderResourceRegistry::AllocateResourceVersion() noexcept {
    const std::uint64_t version = nextResourceVersion_++;
    if (nextResourceVersion_ == 0U) {
        nextResourceVersion_ = 1U;
    }
    return version;
}

} // namespace kb::render
