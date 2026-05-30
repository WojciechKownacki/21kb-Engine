#pragma once

#include "rendering/HeroIconCatalog.hpp"

#if defined(_WIN32)
#pragma warning(push, 0)
#include <objidl.h>
#include <propidl.h>
#include <gdiplus.h>
#pragma warning(pop)
#endif

namespace kb::editor {

#if defined(_WIN32)

class HeroIconPathPainter {
public:
    HeroIconPathPainter() = delete;

    static void Paint(Gdiplus::Graphics& graphics, Gdiplus::GraphicsPath& path, const HeroIconPath& iconPath, const Gdiplus::Color& color, float strokeWidth);
};

#endif

} // namespace kb::editor
