#include "engine/ecs/MutableComponentBorrowLocks.hpp"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>

namespace kb::ecs {

MutableComponentBorrowLocks::Guard::Guard(MutableComponentBorrowLocks& locks, std::size_t token) noexcept
    : locks_(&locks)
    , token_(token) {}

MutableComponentBorrowLocks::Guard::~Guard() {
    Release();
}

MutableComponentBorrowLocks::Guard::Guard(Guard&& other) noexcept
    : locks_(std::exchange(other.locks_, nullptr))
    , token_(std::exchange(other.token_, 0)) {}

MutableComponentBorrowLocks::Guard& MutableComponentBorrowLocks::Guard::operator=(Guard&& other) noexcept {
    if (this != &other) {
        Release();
        locks_ = std::exchange(other.locks_, nullptr);
        token_ = std::exchange(other.token_, 0);
    }
    return *this;
}

void MutableComponentBorrowLocks::Guard::Release() noexcept {
    if (locks_ != nullptr) {
        locks_->Release(token_);
        locks_ = nullptr;
        token_ = 0;
    }
}

bool MutableComponentBorrowLocks::Guard::Active() const noexcept {
    return locks_ != nullptr;
}

MutableComponentBorrowLocks::Guard MutableComponentBorrowLocks::Acquire(std::span<const MutableComponentBorrowRange> ranges) {
#if defined(NDEBUG)
    static_cast<void>(ranges);
    return {};
#else
    if (ranges.empty()) {
        return {};
    }

    std::vector<ActiveBorrow> pendingBorrows;
    pendingBorrows.reserve(ranges.size());

    std::lock_guard lock{ mutex_ };
    const std::size_t token = nextToken_++;
    for (const MutableComponentBorrowRange& range : ranges) {
        pendingBorrows.push_back(BuildBorrow(token, range));
    }

    for (const ActiveBorrow& pending : pendingBorrows) {
        for (const ActiveBorrow& active : activeBorrows_) {
            ValidateNoConflict(active, pending);
        }
    }

    activeBorrows_.insert(activeBorrows_.end(), pendingBorrows.begin(), pendingBorrows.end());
    return Guard{ *this, token };
#endif
}

void MutableComponentBorrowLocks::Clear() noexcept {
#if !defined(NDEBUG)
    std::lock_guard lock{ mutex_ };
    activeBorrows_.clear();
#endif
}

std::size_t MutableComponentBorrowLocks::ActiveBorrowCount() const noexcept {
#if defined(NDEBUG)
    return 0;
#else
    std::lock_guard lock{ mutex_ };
    return activeBorrows_.size();
#endif
}

void MutableComponentBorrowLocks::Release(std::size_t token) noexcept {
#if defined(NDEBUG)
    static_cast<void>(token);
#else
    std::lock_guard lock{ mutex_ };
    std::erase_if(activeBorrows_, [token](const ActiveBorrow& borrow) {
        return borrow.token == token;
    });
#endif
}

#if !defined(NDEBUG)
MutableComponentBorrowLocks::ActiveBorrow MutableComponentBorrowLocks::BuildBorrow(
    std::size_t token,
    const MutableComponentBorrowRange& range) {
    if (range.componentId == 0 || range.data == nullptr || range.bytes == 0) {
        throw std::logic_error("ECS mutable component borrow received an invalid range");
    }

    const auto begin = reinterpret_cast<std::uintptr_t>(range.data);
    if (begin > std::numeric_limits<std::uintptr_t>::max() - range.bytes) {
        throw std::logic_error("ECS mutable component borrow range overflowed");
    }

    return ActiveBorrow{
        .token = token,
        .componentId = range.componentId,
        .begin = begin,
        .end = begin + range.bytes,
    };
}

bool MutableComponentBorrowLocks::Overlaps(const ActiveBorrow& left, const ActiveBorrow& right) noexcept {
    return left.begin < right.end && right.begin < left.end;
}

void MutableComponentBorrowLocks::ValidateNoConflict(const ActiveBorrow& active, const ActiveBorrow& pending) {
    if (active.componentId != pending.componentId || !Overlaps(active, pending)) {
        return;
    }

    throw std::logic_error(
        "ECS mutable component borrow conflict on component " + std::to_string(pending.componentId));
}
#endif

} // namespace kb::ecs
