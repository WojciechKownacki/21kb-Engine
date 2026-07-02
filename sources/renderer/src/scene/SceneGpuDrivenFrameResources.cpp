#include "kb/render/scene/SceneGpuDrivenFrameResources.hpp"

#include <algorithm>
#include <array>
#include <cstring>

namespace kb::render {
namespace {

struct PackedGpuDrivenMetadata {
    std::array<float, 4> values{};
};

[[nodiscard]] bgfx::VertexLayout GpuDrivenVec4Layout() {
    bgfx::VertexLayout layout{};
    layout.begin()
        .add(bgfx::Attrib::Position, 4, bgfx::AttribType::Float)
        .end();
    return layout;
}

[[nodiscard]] std::uint32_t NextCapacity(std::uint32_t requestedCapacity) noexcept {
    constexpr std::uint32_t kMinCapacity = 64U;
    std::uint32_t capacity = kMinCapacity;
    while (capacity < requestedCapacity) {
        capacity *= 2U;
    }
    return capacity;
}

[[nodiscard]] const bgfx::Memory* CopyMemory(const void* source, std::uint32_t byteCount) noexcept {
    const bgfx::Memory* memory = bgfx::alloc(byteCount);
    if (memory == nullptr || memory->data == nullptr) {
        return nullptr;
    }
    std::memcpy(memory->data, source, byteCount);
    return memory;
}

} // namespace

SceneGpuDrivenFrameResources::~SceneGpuDrivenFrameResources() {
    Shutdown();
}

void SceneGpuDrivenFrameResources::Shutdown() noexcept {
    DestroyBuffers();
}

SceneGpuDrivenFrameBatch SceneGpuDrivenFrameResources::Upload(std::span<const SceneGpuDrivenInputRecord> records) {
    SceneGpuDrivenFrameBatch batch{};
    if (records.empty()) {
        return batch;
    }
    const std::uint32_t recordCount = static_cast<std::uint32_t>(std::min<std::size_t>(records.size(), UINT32_MAX));
    if (!EnsureCapacity(recordCount)) {
        return batch;
    }

    std::vector<std::array<float, 4>> bounds(recordCount);
    std::vector<PackedGpuDrivenMetadata> metadata(recordCount);
    for (std::uint32_t index = 0; index < recordCount; ++index) {
        const SceneGpuDrivenInputRecord& record = records[index];
        bounds[index] = record.worldBounds;
        metadata[index] = PackedGpuDrivenMetadata{
            .values = {
                record.drawCommandIndex == UINT32_MAX ? -1.0F : static_cast<float>(record.drawCommandIndex),
                static_cast<float>(record.lodLevel),
                static_cast<float>(record.firstMeshlet),
                static_cast<float>(record.meshletCount),
            },
        };
    }

    const std::uint32_t boundsBytes = static_cast<std::uint32_t>(bounds.size() * sizeof(bounds[0]));
    const std::uint32_t metadataBytes = static_cast<std::uint32_t>(metadata.size() * sizeof(metadata[0]));
    const bgfx::Memory* boundsMemory = CopyMemory(bounds.data(), boundsBytes);
    const bgfx::Memory* metadataMemory = CopyMemory(metadata.data(), metadataBytes);
    if (boundsMemory == nullptr || metadataMemory == nullptr) {
        return batch;
    }

    bgfx::update(boundsBuffer_, 0U, boundsMemory);
    bgfx::update(metadataBuffer_, 0U, metadataMemory);

    batch.boundsBuffer = boundsBuffer_;
    batch.metadataBuffer = metadataBuffer_;
    batch.predicateBuffer = predicateBuffer_;
    batch.visibleListBuffer = visibleListBuffer_;
    batch.counterBuffer = counterBuffer_;
    batch.instanceCount = recordCount;
    batch.capacity = capacity_;
    batch.uploadBytes = static_cast<std::uint64_t>(boundsBytes) +
        static_cast<std::uint64_t>(metadataBytes);
    return batch;
}

bool SceneGpuDrivenFrameResources::EnsureCapacity(std::uint32_t requestedCapacity) {
    if (requestedCapacity <= capacity_ &&
        bgfx::isValid(boundsBuffer_) &&
        bgfx::isValid(metadataBuffer_) &&
        bgfx::isValid(predicateBuffer_) &&
        bgfx::isValid(visibleListBuffer_) &&
        bgfx::isValid(counterBuffer_)) {
        return true;
    }

    DestroyBuffers();
    capacity_ = NextCapacity(requestedCapacity);
    const bgfx::VertexLayout vec4Layout = GpuDrivenVec4Layout();
    boundsBuffer_ = bgfx::createDynamicVertexBuffer(capacity_, vec4Layout, BGFX_BUFFER_COMPUTE_READ);
    metadataBuffer_ = bgfx::createDynamicVertexBuffer(capacity_, vec4Layout, BGFX_BUFFER_COMPUTE_READ);
    predicateBuffer_ = bgfx::createDynamicIndexBuffer(capacity_, BGFX_BUFFER_INDEX32 | BGFX_BUFFER_COMPUTE_READ_WRITE);
    visibleListBuffer_ = bgfx::createDynamicIndexBuffer(capacity_, BGFX_BUFFER_INDEX32 | BGFX_BUFFER_COMPUTE_READ_WRITE);
    counterBuffer_ = bgfx::createDynamicIndexBuffer(4U, BGFX_BUFFER_INDEX32 | BGFX_BUFFER_COMPUTE_READ_WRITE);
    if (!bgfx::isValid(boundsBuffer_) || !bgfx::isValid(metadataBuffer_) ||
        !bgfx::isValid(predicateBuffer_) || !bgfx::isValid(visibleListBuffer_) ||
        !bgfx::isValid(counterBuffer_)) {
        DestroyBuffers();
        return false;
    }

    return true;
}

void SceneGpuDrivenFrameResources::DestroyBuffers() noexcept {
    if (bgfx::isValid(counterBuffer_)) {
        bgfx::destroy(counterBuffer_);
        counterBuffer_ = BGFX_INVALID_HANDLE;
    }
    if (bgfx::isValid(visibleListBuffer_)) {
        bgfx::destroy(visibleListBuffer_);
        visibleListBuffer_ = BGFX_INVALID_HANDLE;
    }
    if (bgfx::isValid(predicateBuffer_)) {
        bgfx::destroy(predicateBuffer_);
        predicateBuffer_ = BGFX_INVALID_HANDLE;
    }
    if (bgfx::isValid(metadataBuffer_)) {
        bgfx::destroy(metadataBuffer_);
        metadataBuffer_ = BGFX_INVALID_HANDLE;
    }
    if (bgfx::isValid(boundsBuffer_)) {
        bgfx::destroy(boundsBuffer_);
        boundsBuffer_ = BGFX_INVALID_HANDLE;
    }
    capacity_ = 0;
}

} // namespace kb::render
