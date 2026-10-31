#include "script/lua/PucLuaMemoryArena.hpp"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <limits>

namespace kb::script {

struct alignas(std::max_align_t) PucLuaMemoryBlock {
    PucLuaMemoryBlock* previous = nullptr;
    PucLuaMemoryBlock* next = nullptr;
    std::size_t capacity = 0U;
    std::size_t size = 0U;
    std::size_t pins = 0U;
    bool live = true;
};

PucLuaMemoryArena::~PucLuaMemoryArena() {
    while (blocks_ != nullptr) Free(blocks_);
}

void PucLuaMemoryArena::Free(PucLuaMemoryBlock* block) noexcept {
    if (block->previous != nullptr) block->previous->next = block->next;
    else blocks_ = block->next;
    if (block->next != nullptr) block->next->previous = block->previous;
    residentBytes_ -= sizeof(PucLuaMemoryBlock) + block->capacity;
    std::free(block);
}

void PucLuaMemoryArena::Release(PucLuaMemoryBlock* block) noexcept {
    liveBytes_ -= block->size;
    block->live = false;
    if (block->pins == 0U) Free(block);
}

void* PucLuaMemoryArena::Resize(void* pointer, std::size_t size, std::size_t limit, bool enforce, bool& refused) noexcept {
    auto* old = pointer == nullptr ? nullptr : static_cast<PucLuaMemoryBlock*>(pointer) - 1;
    if (size == 0U) {
        if (old != nullptr) Release(old);
        return nullptr;
    }
    if (old != nullptr && size <= old->capacity) {
        liveBytes_ = liveBytes_ - old->size + size;
        old->size = size;
        return pointer;
    }
    if (size > std::numeric_limits<std::size_t>::max() - sizeof(PucLuaMemoryBlock)) return nullptr;
    const std::size_t growth = old != nullptr && old->pins == 0U ? size - old->capacity : sizeof(PucLuaMemoryBlock) + size;
    const std::size_t resident = ResidentBytes();
    if (enforce && limit != 0U && (resident >= limit || growth > limit - resident)) {
        refused = true;
        return nullptr;
    }
    if (old != nullptr && old->pins == 0U) {
        const std::size_t previousSize = old->size;
        const std::size_t previousCapacity = old->capacity;
        auto* resized = static_cast<PucLuaMemoryBlock*>(std::realloc(old, sizeof(PucLuaMemoryBlock) + size));
        if (resized == nullptr) return nullptr;
        if (resized->previous != nullptr) resized->previous->next = resized;
        else blocks_ = resized;
        if (resized->next != nullptr) resized->next->previous = resized;
        resized->capacity = resized->size = size;
        residentBytes_ += size - previousCapacity;
        liveBytes_ = liveBytes_ - previousSize + size;
        return resized + 1;
    }
    auto* created = static_cast<PucLuaMemoryBlock*>(std::malloc(sizeof(PucLuaMemoryBlock) + size));
    if (created == nullptr) return nullptr;
    *created = PucLuaMemoryBlock{.next = blocks_, .capacity = size, .size = size};
    if (blocks_ != nullptr) blocks_->previous = created;
    blocks_ = created;
    residentBytes_ += sizeof(PucLuaMemoryBlock) + size;
    liveBytes_ += size;
    if (old != nullptr) {
        std::memcpy(created + 1, pointer, std::min(size, old->size));
        Release(old);
    }
    return created + 1;
}

std::unique_ptr<PucLuaHeapImage> PucLuaMemoryArena::Capture(std::size_t limit, std::string& error) {
    error.clear();
    std::size_t count = 0U;
    std::size_t payload = 0U;
    for (auto* block = blocks_; block != nullptr; block = block->next) {
        if (block->live) { ++count; payload += block->size; }
    }
    const std::size_t estimate = sizeof(PucLuaHeapImage) + payload + count * sizeof(PucLuaHeapImage::Record);
    const std::size_t resident = ResidentBytes();
    if (limit != 0U && (resident >= limit || estimate > limit - resident)) {
        error = "Lua checkpoint history exceeds the configured memory budget";
        return {};
    }
    try {
        auto image = std::make_unique<PucLuaHeapImage>();
        image->arena_ = shared_from_this();
        image->records_.reserve(count);
        image->ownedBytes_ = sizeof(PucLuaHeapImage) + image->records_.capacity() * sizeof(PucLuaHeapImage::Record);
        for (auto* block = blocks_; block != nullptr; block = block->next) {
            if (!block->live) continue;
            auto& record = image->records_.emplace_back();
            record.block = block;
            record.size = block->size;
            const auto* data = reinterpret_cast<const std::byte*>(block + 1);
            record.bytes.assign(data, data + block->size);
            image->payloadBytes_ += block->size;
            image->ownedBytes_ += record.bytes.capacity();
        }
        if (limit != 0U && image->ownedBytes_ > limit - resident) {
            error = "Lua checkpoint history exceeds the configured memory budget";
            return {};
        }
        for (const auto& record : image->records_) ++record.block->pins;
        imageBytes_ += image->ownedBytes_;
        image->pinned_ = true;
        return image;
    } catch (const std::bad_alloc&) {
        error = "Lua checkpoint history allocation failed";
        return {};
    }
}

bool PucLuaMemoryArena::Restore(const PucLuaHeapImage& image, std::string& error) {
    if (image.arena_.get() != this || !image.pinned_) { error = "Lua heap image belongs to another allocator"; return false; }
    // Image blocks cannot have moved or shrunk while pinned by this image.
    for (const auto& record : image.records_) {
        if (record.block->pins == 0U || record.size > record.block->capacity) {
            error = "Lua heap image address retention was violated"; return false;
        }
    }
    for (auto* block = blocks_; block != nullptr; block = block->next) block->live = false;
    liveBytes_ = 0U;
    for (const auto& record : image.records_) {
        std::memcpy(record.block + 1, record.bytes.data(), record.size);
        record.block->live = true;
        record.block->size = record.size;
        liveBytes_ += record.size;
    }
    auto* block = blocks_;
    while (block != nullptr) {
        auto* next = block->next;
        if (!block->live && block->pins == 0U) Free(block);
        block = next;
    }
    error.clear();
    return true;
}

PucLuaHeapImage::~PucLuaHeapImage() {
    if (!pinned_) return;
    arena_->imageBytes_ -= ownedBytes_;
    for (const auto& record : records_) {
        auto* block = record.block;
        --block->pins;
        if (block->pins == 0U && !block->live) arena_->Free(block);
    }
}

} // namespace kb::script
