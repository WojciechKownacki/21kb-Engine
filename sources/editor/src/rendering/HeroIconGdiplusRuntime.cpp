#include "rendering/HeroIconGdiplusRuntime.hpp"

#if defined(_WIN32)
#pragma warning(push, 0)
#include <objidl.h>
#include <propidl.h>
#include <gdiplus.h>
#pragma warning(pop)
#endif

namespace kb::editor {

#if defined(_WIN32)
namespace {

class GdiplusRuntimeScope {
public:
    GdiplusRuntimeScope() {
        Gdiplus::GdiplusStartupInput input;
        static_cast<void>(Gdiplus::GdiplusStartup(&token_, &input, nullptr));
    }

    ~GdiplusRuntimeScope() {
        if (token_ != 0) {
            Gdiplus::GdiplusShutdown(token_);
        }
    }

    GdiplusRuntimeScope(const GdiplusRuntimeScope&) = delete;
    GdiplusRuntimeScope& operator=(const GdiplusRuntimeScope&) = delete;

private:
    ULONG_PTR token_ = 0;
};

} // namespace

void HeroIconGdiplusRuntime::EnsureStarted() {
    static GdiplusRuntimeScope runtime;
}

#endif

} // namespace kb::editor
