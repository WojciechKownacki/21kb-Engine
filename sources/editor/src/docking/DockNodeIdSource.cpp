#include "docking/DockNodeIdSource.hpp"

namespace kb::editor {

DockNodeIdSource::DockNodeIdSource(DockNextNodeIdFn nextNodeId, void* context) noexcept
    : nextNodeId_(nextNodeId)
    , context_(context) {}

std::uint32_t DockNodeIdSource::Next() const noexcept {
    return nextNodeId_ == nullptr ? 0U : nextNodeId_(context_);
}

} // namespace kb::editor
