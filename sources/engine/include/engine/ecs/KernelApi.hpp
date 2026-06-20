#pragma once

// Canonical hot-kernel API contract from the ECS roadmap (section 4.4). These
// are the low-level, type-erased primitives a release kernel receives:
//
//   using KernelFn = void (*)(KernelContext, KernelRange, ColumnBundle) noexcept;
//
// They carry no ownership, no allocation, no dispatch -- just the worker context,
// the row range to process, and the column base pointers. Higher-level helpers
// (UnsafeHotQuery, HotKernel templates) build on the same data; this header gives
// it the spec's exact shape so kernels can be expressed as plain function
// pointers with zero abstraction overhead.

#include "engine/ecs/KernelColumn.hpp"

#include <array>
#include <cassert>
#include <cstddef>

namespace kb::ecs {

// Per-invocation worker context: which worker, out of how many, and the frame dt.
struct KernelContext {
    std::uint32_t workerIndex = 0U;
    std::uint32_t workerCount = 1U;
    float dt = 0.0F;
};

// Half-open row range [begin, begin + count) within a chunk's columns.
struct KernelRange {
    std::size_t begin = 0U;
    std::size_t count = 0U;

    [[nodiscard]] constexpr std::size_t End() const noexcept {
        return begin + count;
    }

    [[nodiscard]] constexpr bool Empty() const noexcept {
        return count == 0U;
    }
};

inline constexpr std::size_t kMaxKernelColumns = 16U;

// Type-erased set of column base pointers handed to a kernel. The kernel knows
// the static layout (which index is which component) and reinterprets each base.
// No bounds beyond the column count are tracked here: the KernelRange defines the
// rows, exactly as a hand-written HPC kernel would expect.
class ColumnBundle {
public:
    constexpr ColumnBundle() noexcept = default;

    void Add(void* column) noexcept {
        assert(count_ < kMaxKernelColumns && "ColumnBundle exceeded the maximum column count");
        columns_[count_++] = column;
    }

    [[nodiscard]] std::size_t Count() const noexcept {
        return count_;
    }

    [[nodiscard]] void* Raw(std::size_t index) const noexcept {
        assert(index < count_ && "ColumnBundle column index is out of range");
        return columns_[index];
    }

    template <typename T>
    [[nodiscard]] T* Column(std::size_t index) const noexcept {
        assert(index < count_ && "ColumnBundle column index is out of range");
        return static_cast<T*>(columns_[index]);
    }

    template <typename T, std::size_t Alignment = kKernelColumnAlignment>
    [[nodiscard]] AlignedColumn<T, Alignment> Aligned(std::size_t index, std::size_t rowCount) const noexcept {
        return AlignedColumn<T, Alignment>{ Column<T>(index), rowCount };
    }

private:
    std::array<void*, kMaxKernelColumns> columns_{};
    std::size_t count_ = 0U;
};

// The canonical release-kernel signature: a plain function pointer, inlinable,
// with no allocation, no virtual dispatch, and no type-erased callable wrappers.
using KernelFn = void (*)(KernelContext, KernelRange, ColumnBundle) noexcept;

} // namespace kb::ecs
