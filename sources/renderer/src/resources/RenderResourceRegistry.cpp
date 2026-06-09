#include "kb/render/resources/RenderResourceRegistry.hpp"

#include "resources/RenderMaterialResourceBuilder.hpp"
#include "resources/RenderMeshResourceBuilder.hpp"
#include "resources/RenderResourceReleaser.hpp"
#include "resources/RenderTextureResourceBuilder.hpp"

#include <utility>

namespace kb::render {
namespace {

constexpr std::uint64_t kDeferredDestroyFrameDelay = 3U;

} // namespace

RenderResourceRegistry::RenderResourceRegistry() = default;

RenderResourceRegistry::~RenderResourceRegistry() {
    Shutdown();
}

RenderMeshHandle RenderResourceRegistry::RegisterMesh(const RenderMeshDesc& desc) {
    if (!RenderMeshResourceBuilder::IsValidDesc(desc)) {
        return {};
    }

    const bgfx::VertexLayout layout = RenderStaticMeshVertexLayout(desc.vertexFormat);
    const void* vertexData = RenderMeshResourceBuilder::VertexData(desc);
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

    const std::uint32_t slotIndex = meshes_.Allocate();
    RenderMeshResource resource = RenderMeshResourceBuilder::Build(desc, vertexBuffer, indexBuffer);
    resource.version = AllocateResourceVersion();
    meshes_.Activate(slotIndex, std::move(resource));
    return RenderMeshHandle{ detail::MakeRenderHandleValue(slotIndex, meshes_.Generation(slotIndex)) };
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
    const std::uint32_t slotIndex = materials_.Allocate();
    RenderMaterialResource resource = RenderMaterialResourceBuilder::Build(desc);
    resource.version = AllocateResourceVersion();
    materials_.Activate(slotIndex, std::move(resource));
    return RenderMaterialHandle{ detail::MakeRenderHandleValue(slotIndex, materials_.Generation(slotIndex)) };
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
        return {};
    }

    bgfx::TextureHandle texture = bgfx::createTexture2D(desc.width, desc.height, false, 1, desc.format, desc.flags, desc.memory);
    if (!bgfx::isValid(texture)) {
        return {};
    }

    const std::uint32_t slotIndex = textures_.Allocate();
    RenderTextureResource resource = RenderTextureResourceBuilder::Build(desc, texture);
    resource.version = AllocateResourceVersion();
    textures_.Activate(slotIndex, std::move(resource));
    return RenderTextureHandle{ detail::MakeRenderHandleValue(slotIndex, textures_.Generation(slotIndex)) };
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
