#pragma once

#include "docking/FloatingWindowRegistry.hpp"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif

#include <cstdint>
#include <vector>

namespace kb::editor {

class FloatingWindowDestroyer {
public:
#if defined(_WIN32)
    // Destroys the windows of the groups that are no longer in `liveGroups`.
    static void DestroyOrphaned(FloatingWindowRegistry& registry, const std::vector<std::uint32_t>& liveGroups);
    static void DestroyAll(FloatingWindowRegistry& registry);
#endif
};

} // namespace kb::editor
