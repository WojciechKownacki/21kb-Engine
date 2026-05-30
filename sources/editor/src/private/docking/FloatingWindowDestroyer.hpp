#pragma once

#include "docking/FloatingWindowRegistry.hpp"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif

#include <cstdint>

namespace kb::editor {

class FloatingWindowDestroyer {
public:
#if defined(_WIN32)
    static void DestroyPanel(FloatingWindowRegistry& registry, std::uint32_t panelId);
    static void DestroyAll(FloatingWindowRegistry& registry);
#endif
};

} // namespace kb::editor
