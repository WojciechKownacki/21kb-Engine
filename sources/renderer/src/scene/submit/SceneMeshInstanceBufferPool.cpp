#include "scene/submit/SceneMeshInstanceBufferPool.hpp"
#include "kb/render/scene/RenderInstanceBuffer.hpp"
#include <algorithm>
#include <limits>

namespace kb::render {

std::size_t SceneMeshInstanceBufferPool::OwnerKeyHash::operator()(const OwnerKey& key) const noexcept {
    std::uint64_t hash = key.owner.batchId;
    for (const auto part : {std::uint64_t(key.owner.commandIndex), std::uint64_t(key.owner.pass),
            std::uint64_t(key.owner.viewId), std::uint64_t(key.occurrence)}) {
        hash ^= part + 0x9e3779b97f4a7c15ULL + (hash << 6U) + (hash >> 2U);
    }
    return static_cast<std::size_t>(hash);
}

SceneMeshInstanceBufferPool::Slot& SceneMeshInstanceBufferPool::Acquire(SceneMeshInstanceBufferOwner owner) {
    if (owner.batchId == 0U) {
        if (usedSlots_ == slots_.size()) slots_.emplace_back();
        return slots_[usedSlots_++];
    }
    OwnerKey key{owner};
    for (;;) {
        auto& slot = ownedSlots_[key];
        // Repeated submissions in one frame need separate allocations: bgfx may
        // process their uploads before either draw consumes the data.
        if (slot.lastUsedFrame != frameIndex_) {
            slot.lastUsedFrame = frameIndex_;
            return slot;
        }
        ++key.occurrence;
    }
}

void SceneMeshInstanceBufferPool::EndFrame() noexcept {
    usedSlots_ = 0U;
    ++frameIndex_;
    for (auto it = ownedSlots_.begin(); it != ownedSlots_.end();) {
        if (frameIndex_ - it->second.lastUsedFrame > 3U) {
            if (bgfx::isValid(it->second.buffer)) bgfx::destroy(it->second.buffer);
            it = ownedSlots_.erase(it);
        } else ++it;
    }
}

SceneMeshInstanceBufferPool::~SceneMeshInstanceBufferPool() {
    Shutdown();
}

bgfx::DynamicVertexBufferHandle SceneMeshInstanceBufferPool::Upload(
    std::span<const SceneRenderMeshInstance> instances,
    const RenderMaterialResource* material,
    bool encodeShadowReceiver,
    std::uint64_t instanceRevision, SceneMeshInstanceBufferOwner owner) {
    lastUploadBytes_ = 0U;
    constexpr std::uint32_t stride = RenderInstanceBuffer::Stride();
    if (instances.empty() || instances.size() > std::numeric_limits<std::uint32_t>::max() / stride) {
        return BGFX_INVALID_HANDLE;
    }
    const std::uint32_t count = static_cast<std::uint32_t>(instances.size());
    Slot& slot = Acquire(owner);
    if (instanceRevision != 0U && instanceRevision == slot.instanceRevision &&
        bgfx::isValid(slot.buffer) && count == slot.count && encodeShadowReceiver == slot.encodeShadowReceiver) {
        return slot.buffer;
    }
    if (!bgfx::isValid(slot.buffer) || count > slot.capacity) {
        const std::uint32_t maxCapacity = std::numeric_limits<std::uint32_t>::max() / stride;
        const std::uint32_t capacity = std::max(count,
            slot.capacity <= maxCapacity / 2U ? slot.capacity * 2U : count);
        bgfx::VertexLayout layout;
        layout.begin()
            .add(bgfx::Attrib::TexCoord0, 4, bgfx::AttribType::Float)
            .add(bgfx::Attrib::TexCoord1, 4, bgfx::AttribType::Float)
            .add(bgfx::Attrib::TexCoord2, 4, bgfx::AttribType::Float)
            .add(bgfx::Attrib::TexCoord3, 4, bgfx::AttribType::Float)
            .add(bgfx::Attrib::TexCoord4, 4, bgfx::AttribType::Float)
            .end();
        const bgfx::DynamicVertexBufferHandle replacement = bgfx::createDynamicVertexBuffer(capacity, layout);
        if (!bgfx::isValid(replacement)) {
            return BGFX_INVALID_HANDLE;
        }
        if (bgfx::isValid(slot.buffer)) {
            bgfx::destroy(slot.buffer);
        }
        slot.buffer = replacement;
        slot.capacity = capacity;
    }

    const bgfx::Memory* memory = bgfx::alloc(count * stride);
    RenderInstanceBuffer::Copy(
        std::span<RenderInstanceData>{reinterpret_cast<RenderInstanceData*>(memory->data), count},
        instances, material, encodeShadowReceiver);
    bgfx::update(slot.buffer, 0U, memory);
    slot.instanceRevision = instanceRevision;
    slot.count = count;
    slot.encodeShadowReceiver = encodeShadowReceiver;
    lastUploadBytes_ = static_cast<std::uint64_t>(count) * stride;
    return slot.buffer;
}

void SceneMeshInstanceBufferPool::Shutdown() noexcept {
    for (Slot& slot : slots_) {
        if (bgfx::isValid(slot.buffer)) {
            bgfx::destroy(slot.buffer);
            slot.buffer = BGFX_INVALID_HANDLE;
        }
    }
    for (auto& [key, slot] : ownedSlots_) {
        static_cast<void>(key);
        if (bgfx::isValid(slot.buffer)) bgfx::destroy(slot.buffer);
    }
    ownedSlots_.clear();
    slots_.clear();
    usedSlots_ = 0U;
    frameIndex_ = 0U;
    lastUploadBytes_ = 0U;
}

} // namespace kb::render
