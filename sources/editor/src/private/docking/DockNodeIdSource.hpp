#pragma once

#include <cstdint>

namespace kb::editor {

using DockNextNodeIdFn = std::uint32_t (*)(void* context) noexcept;

class DockNodeIdSource {
public:
    DockNodeIdSource(DockNextNodeIdFn nextNodeId, void* context) noexcept;

    [[nodiscard]] std::uint32_t Next() const noexcept;

private:
    DockNextNodeIdFn nextNodeId_ = nullptr;
    void* context_ = nullptr;
};

} // namespace kb::editor
