#pragma once

#if defined(_WIN32)

namespace kb::editor {

class HeroIconGdiplusRuntime {
public:
    HeroIconGdiplusRuntime() = delete;

    static void EnsureStarted();
};

} // namespace kb::editor

#endif
