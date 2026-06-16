#pragma once

#include "engine/ecs/ComponentId.hpp"

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <span>
#include <vector>

namespace kb::ecs {

struct MutableComponentBorrowRange {
    ComponentId componentId = 0;
    const void* data = nullptr;
    std::size_t bytes = 0;
};

class MutableComponentBorrowLocks {
public:
    class Guard {
    public:
        Guard() noexcept = default;
        ~Guard();

        Guard(const Guard&) = delete;
        Guard& operator=(const Guard&) = delete;
        Guard(Guard&& other) noexcept;
        Guard& operator=(Guard&& other) noexcept;

        void Release() noexcept;
        [[nodiscard]] bool Active() const noexcept;

    private:
        Guard(MutableComponentBorrowLocks& locks, std::size_t token) noexcept;

        MutableComponentBorrowLocks* locks_ = nullptr;
        std::size_t token_ = 0;

        friend class MutableComponentBorrowLocks;
    };

    [[nodiscard]] Guard Acquire(std::span<const MutableComponentBorrowRange> ranges);
    void Clear() noexcept;
    [[nodiscard]] std::size_t ActiveBorrowCount() const noexcept;

private:
    struct ActiveBorrow {
        std::size_t token = 0;
        ComponentId componentId = 0;
        std::uintptr_t begin = 0;
        std::uintptr_t end = 0;
    };

    void Release(std::size_t token) noexcept;

#if !defined(NDEBUG)
    [[nodiscard]] static ActiveBorrow BuildBorrow(std::size_t token, const MutableComponentBorrowRange& range);
    [[nodiscard]] static bool Overlaps(const ActiveBorrow& left, const ActiveBorrow& right) noexcept;
    static void ValidateNoConflict(const ActiveBorrow& active, const ActiveBorrow& pending);

    mutable std::mutex mutex_;
    std::vector<ActiveBorrow> activeBorrows_;
    std::size_t nextToken_ = 1;
#endif
};

} // namespace kb::ecs
