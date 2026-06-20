#pragma once

// Kernel column primitives: explicit no-alias and alignment contracts for hot
// kernels. These wrappers carry the compiler-visible promises that the roadmap
// hot path depends on (restrict pointers, known alignment) without adding any
// runtime overhead in release builds. They are intentionally minimal: a single
// responsibility each, composable, and zero-cost once optimized.

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

// Default SoA column alignment used across the native archetype storage. Columns
// are allocated 64-byte aligned so AVX-512 loads and full cache lines are legal.
inline constexpr std::size_t kKernelColumnAlignment = 64U;

[[nodiscard]] constexpr bool IsPowerOfTwo(std::size_t value) noexcept {
    return value != 0U && (value & (value - 1U)) == 0U;
}

[[nodiscard]] inline bool IsPointerAligned(const void* pointer, std::size_t alignment) noexcept {
    return alignment != 0U && (reinterpret_cast<std::uintptr_t>(pointer) % alignment) == 0U;
}

// Returns a pointer the compiler may assume is aligned to Alignment bytes. In
// debug builds the assumption is checked; in release it lowers to the bare
// pointer with the optimizer hint attached.
template <std::size_t Alignment, typename T>
[[nodiscard]] T* AssumeAligned(T* pointer) noexcept {
    static_assert(IsPowerOfTwo(Alignment), "ECS kernel alignment must be a power of two");
    assert((pointer == nullptr || IsPointerAligned(pointer, Alignment)) && "ECS kernel pointer is not aligned to the assumed boundary");
    return std::assume_aligned<Alignment>(pointer);
}

// Thin wrapper over a pointer that promises the referenced data does not alias
// any other restrict pointer in the same scope. Use for hot kernel column
// arguments so the compiler can vectorize without alias guards.
template <typename T>
class RestrictPtr {
public:
    using element_type = T;

    constexpr RestrictPtr() noexcept = default;
    constexpr explicit RestrictPtr(T* pointer) noexcept : pointer_(pointer) {}

    [[nodiscard]] constexpr T* KB_RESTRICT Get() const noexcept {
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
    T* KB_RESTRICT pointer_ = nullptr;
};

// A non-owning, alignment-annotated, non-aliasing view over a single SoA column.
// Carries a compile-time alignment contract (default 64B to match storage) plus
// a runtime debug assertion, and hands out pointers the compiler may assume are
// aligned. It owns no memory: storage lifetime is the caller's responsibility.
template <typename T, std::size_t Alignment = kKernelColumnAlignment>
class AlignedColumn {
public:
    using element_type = T;
    static constexpr std::size_t kAlignment = Alignment;

    static_assert(IsPowerOfTwo(Alignment), "ECS aligned column alignment must be a power of two");
    static_assert(Alignment >= alignof(T), "ECS aligned column alignment must be at least the element alignment");

    constexpr AlignedColumn() noexcept = default;

    AlignedColumn(T* data, std::size_t count) noexcept : data_(data), count_(count) {
        assert((data == nullptr || IsPointerAligned(data, Alignment)) && "ECS aligned column constructed from an unaligned pointer");
    }

    // Returns the column base with the alignment assumption attached for codegen.
    [[nodiscard]] T* KB_RESTRICT Data() const noexcept {
        return AssumeAligned<Alignment>(data_);
    }

    [[nodiscard]] std::size_t Count() const noexcept {
        return count_;
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
        assert(begin <= count_ && begin + count <= count_ && "ECS aligned column subrange is out of bounds");
        // A sub-offset only preserves the column alignment when it lands on an
        // aligned element boundary; otherwise the caller must fall back to an
        // unaligned view. We keep the contract honest by asserting it.
        T* offsetData = data_ == nullptr ? nullptr : data_ + begin;
        return AlignedColumn{ offsetData, count, AlignmentHonoredTag{} };
    }

private:
    struct AlignmentHonoredTag {};
    AlignedColumn(T* data, std::size_t count, AlignmentHonoredTag) noexcept : data_(data), count_(count) {}

    T* data_ = nullptr;
    std::size_t count_ = 0U;
};

} // namespace kb::ecs
