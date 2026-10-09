#pragma once

#include <algorithm>
#include <cstddef>

namespace kb::ecs {

// reserve() allocates exactly the requested capacity, so a reserve for a size that grows a little on every call
// (a frame that adds a few entities) reallocates and copies the whole vector each time. Growing geometrically keeps
// the amortized cost of push_back; the capacity never shrinks.
template <typename Vector>
void ReserveGeometric(Vector& values, std::size_t required) {
    if (required > values.capacity()) {
        values.reserve(std::max(required, values.capacity() + values.capacity() / 2U));
    }
}

} // namespace kb::ecs
