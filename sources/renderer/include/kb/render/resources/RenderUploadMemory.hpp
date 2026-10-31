#pragma once

#include <bgfx/bgfx.h>

#include <atomic>
#include <cstdint>
#include <cstring>
#include <memory>
#include <utility>

namespace kb::render {

// Upload payload ownership follows the backend's release callback, including shutdown.
// These are CPU staging bytes, not GPU residency or allocator overhead.
class RenderUploadMemory final {
public:
    [[nodiscard]] const bgfx::Memory* Copy(const void* data, std::uint32_t size) {
        auto block = std::make_unique<Block>(state_, size);
        std::memcpy(block->bytes.get(), data, size);
        const auto pending = state_->pending.fetch_add(size) + size;
        auto peak = state_->peak.load();
        while (peak < pending && !state_->peak.compare_exchange_weak(peak, pending)) {}
        const auto* memory = bgfx::makeRef(block->bytes.get(), size, &Release, block.get());
        static_cast<void>(block.release());
        return memory;
    }

    [[nodiscard]] std::uint64_t PendingBytes() const noexcept { return state_->pending.load(); }
    [[nodiscard]] std::uint64_t PeakBytes() const noexcept { return state_->peak.load(); }

private:
    struct State {
        std::atomic_uint64_t pending{0U};
        std::atomic_uint64_t peak{0U};
    };
    struct Block {
        Block(std::shared_ptr<State> owner, std::uint32_t count)
            : state(std::move(owner)), bytes(std::make_unique_for_overwrite<std::uint8_t[]>(count)), size(count) {}
        std::shared_ptr<State> state;
        std::unique_ptr<std::uint8_t[]> bytes;
        std::uint32_t size;
    };
    static void Release(void*, void* userData) {
        const auto* block = static_cast<Block*>(userData);
        const auto state = block->state;
        const auto size = block->size;
        delete block;
        state->pending.fetch_sub(size);
    }
    std::shared_ptr<State> state_ = std::make_shared<State>();
};

} // namespace kb::render
