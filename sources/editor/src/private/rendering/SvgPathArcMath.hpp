#pragma once

#include "rendering/SvgPathArcCenter.hpp"
#include "rendering/SvgPathArcEndpoint.hpp"

#include <optional>

namespace kb::editor {

class SvgPathArcMath {
public:
    SvgPathArcMath() = delete;

    [[nodiscard]] static std::optional<SvgPathArcCenter> ToCenterArc(SvgPathArcEndpoint endpoint) noexcept;
};

} // namespace kb::editor
