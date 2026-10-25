#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <span>

namespace kb::game {

// The mounted pack borrows these bytes until the runtime has unmounted it.
class WebDownloadBuffer {
public:
    enum class Error { None, SizeLimit, Allocation };
    using Reallocate = void* (*)(void*, std::size_t);
    using Release = void (*)(void*);

    explicit WebDownloadBuffer(std::size_t limit,
        Reallocate reallocate = &std::realloc, Release release = &std::free) noexcept
        : limit_(limit), reallocate_(reallocate), release_(release) {}
    ~WebDownloadBuffer() { Reset(); }
    WebDownloadBuffer(const WebDownloadBuffer&) = delete;
    WebDownloadBuffer& operator=(const WebDownloadBuffer&) = delete;

    [[nodiscard]] bool Reserve(std::size_t capacity) noexcept {
        if (capacity > limit_) { error_ = Error::SizeLimit; return false; }
        if (capacity <= capacity_) return true;
        void* allocation = reallocate_(bytes_, capacity);
        if (allocation == nullptr) { error_ = Error::Allocation; return false; }
        bytes_ = static_cast<std::uint8_t*>(allocation);
        capacity_ = capacity;
        return true;
    }

    [[nodiscard]] std::uint8_t* Append(std::size_t count) noexcept {
        if (count == 0 || count > limit_ - size_) {
            error_ = Error::SizeLimit;
            return nullptr;
        }
        const std::size_t required = size_ + count;
        if (required > capacity_) {
            const std::size_t increment = (std::max)(capacity_ / 2U, std::size_t{64U * 1024U});
            const std::size_t grown = capacity_ + (std::min)(increment, limit_ - capacity_);
            if (!Reserve((std::max)(required, grown))) return nullptr;
        }
        auto* result = bytes_ + size_;
        size_ = required;
        return result;
    }

    void Reset() noexcept {
        if (bytes_ != nullptr) release_(bytes_);
        bytes_ = nullptr;
        size_ = capacity_ = 0;
        error_ = Error::None;
    }
    [[nodiscard]] std::span<const std::uint8_t> Bytes() const noexcept { return {bytes_, size_}; }
    [[nodiscard]] std::size_t Capacity() const noexcept { return capacity_; }
    [[nodiscard]] Error LastError() const noexcept { return error_; }

private:
    std::size_t limit_;
    Reallocate reallocate_;
    Release release_;
    std::uint8_t* bytes_ = nullptr;
    std::size_t size_ = 0;
    std::size_t capacity_ = 0;
    Error error_ = Error::None;
};

} // namespace kb::game
