#pragma once

// Non-owning kernel column views. Alignment hints are issued only when the
// actual pointer satisfies them; offsets and overlapping views remain valid.

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <type_traits>

#if defined(_MSC_VER)
#define KB_RESTRICT __restrict
#elif defined(__GNUC__) || defined(__clang__)
#define KB_RESTRICT __restrict__
#else
#define KB_RESTRICT
#endif

namespace kb::ecs {

// Preferred kernel alignment. Native chunk bases have this alignment, but an
// individual component column is only guaranteed its registered type alignment.
inline constexpr std::size_t kKernelColumnAlignment = 64U;

[[nodiscard]] constexpr bool IsPowerOfTwo(std::size_t value) noexcept {
    return value != 0U && (value & (value - 1U)) == 0U;
}

[[nodiscard]] inline bool IsPointerAligned(const void* pointer, std::size_t alignment) noexcept {
    return alignment != 0U && (reinterpret_cast<std::uintptr_t>(pointer) % alignment) == 0U;
}

// Attach an alignment hint only after checking the actual address. An offset
// column can retain its ordinary element alignment without meeting Alignment.
template <std::size_t Alignment, typename T>
[[nodiscard]] T* AssumeAligned(T* pointer) noexcept {
    static_assert(IsPowerOfTwo(Alignment), "ECS kernel alignment must be a power of two");
    if (pointer != nullptr && IsPointerAligned(pointer, Alignment)) {
        return std::assume_aligned<Alignment>(pointer);
    }
    return pointer;
}

// Compatibility pointer wrapper. A view alone cannot prove that other column
// views do not overlap, so the historical name does not impose a no-alias
// promise. Restricted kernel arguments require a separate disjoint-range proof.
template <typename T>
class RestrictPtr {
public:
    using element_type = T;

    constexpr RestrictPtr() noexcept = default;
    constexpr explicit RestrictPtr(T* pointer) noexcept : pointer_(pointer) {}

    [[nodiscard]] constexpr T* Get() const noexcept {
        return pointer_;
    }

    [[nodiscard]] constexpr T& operator[](std::size_t index) const noexcept {
        return pointer_[index];
    }

    [[nodiscard]] constexpr T* operator->() const noexcept {
        return pointer_;
    }

    [[nodiscard]] constexpr T& operator*() const noexcept {
        return *pointer_;
    }

    [[nodiscard]] constexpr explicit operator bool() const noexcept {
        return pointer_ != nullptr;
    }

private:
    T* pointer_ = nullptr;
};

// A non-owning column view with a preferred alignment. Data() supplies a checked
// alignment hint when possible and an ordinary pointer otherwise. IsAligned()
// lets an intrinsic kernel choose between aligned and unaligned operations.
// Storage lifetime and ordinary element alignment remain the caller's responsibility.
template <typename T, std::size_t Alignment = kKernelColumnAlignment>
class AlignedColumn {
public:
    using element_type = T;
    static constexpr std::size_t kAlignment = Alignment;

    static_assert(IsPowerOfTwo(Alignment), "ECS aligned column alignment must be a power of two");
    static_assert(Alignment >= alignof(T), "ECS aligned column alignment must be at least the element alignment");

    constexpr AlignedColumn() noexcept = default;

    AlignedColumn(T* data, std::size_t count) noexcept : data_(data), count_(count) {}

    // Returns the base; the compiler hint is conditional on its actual alignment.
    [[nodiscard]] T* Data() const noexcept {
        return AssumeAligned<Alignment>(data_);
    }

    [[nodiscard]] std::size_t Count() const noexcept {
        return count_;
    }

    [[nodiscard]] bool IsAligned() const noexcept {
        return data_ == nullptr || IsPointerAligned(data_, Alignment);
    }

    [[nodiscard]] bool Empty() const noexcept {
        return count_ == 0U;
    }

    [[nodiscard]] T& operator[](std::size_t index) const noexcept {
        assert(index < count_ && "ECS aligned column index is out of range");
        return Data()[index];
    }

    [[nodiscard]] RestrictPtr<T> Restrict() const noexcept {
        return RestrictPtr<T>(Data());
    }

    [[nodiscard]] AlignedColumn Subrange(std::size_t begin, std::size_t count) const noexcept {
        assert(begin <= count_ && count <= count_ - begin && "ECS aligned column subrange is out of bounds");
        T* offsetData = data_ == nullptr ? nullptr : data_ + begin;
        return AlignedColumn{ offsetData, count };
    }

private:
    T* data_ = nullptr;
    std::size_t count_ = 0U;
};

} // namespace kb::ecs
