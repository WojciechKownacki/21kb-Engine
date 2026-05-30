#pragma once

#include "kb/editor/docking/DockTypes.hpp"

#include <cstdint>

namespace kb::editor {

class DockTabIndexResolver {
public:
    [[nodiscard]] std::uint32_t Resolve(const DockLayout& layout, std::uint32_t leafId, int x) const noexcept;
};

} // namespace kb::editor
