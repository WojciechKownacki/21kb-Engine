#pragma once

#include "ecs/query/QueryLimits.hpp"

#include <cstddef>
#include <span>

struct ecs_iter_t;

namespace kb::ecs {

class QueryFieldReader {
public:
    [[nodiscard]] static bool Read(
        ecs_iter_t& iterator,
        std::span<const std::size_t> componentSizes,
        QueryComponentPointerBlock& fieldComponents) noexcept;

    [[nodiscard]] static bool ReadMutable(
        ecs_iter_t& iterator,
        std::span<const std::size_t> componentSizes,
        MutableQueryComponentPointerBlock& fieldComponents) noexcept;
};

} // namespace kb::ecs
