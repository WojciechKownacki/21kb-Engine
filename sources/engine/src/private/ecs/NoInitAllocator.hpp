#pragma once

#include <memory>
#include <type_traits>
#include <utility>

namespace kb::ecs {

// An allocator whose default construction leaves trivially copyable elements uninitialized, so resizing a scratch
// vector that is overwritten right away does not first touch every element. Reading an element before writing it is a bug.
template <typename T>
struct NoInitAllocator : std::allocator<T> {
    static_assert(std::is_trivially_copyable_v<T> && std::is_trivially_destructible_v<T>);
    template <typename U>
    struct rebind {
        using other = NoInitAllocator<U>;
    };
    NoInitAllocator() = default;
    template <typename U>
    NoInitAllocator(const NoInitAllocator<U>&) noexcept {}
    template <typename U>
    void construct(U*) noexcept {}
    template <typename U, typename... Args>
    void construct(U* pointer, Args&&... args) {
        ::new (static_cast<void*>(pointer)) U(std::forward<Args>(args)...);
    }
};

} // namespace kb::ecs
