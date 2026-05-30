#pragma once

#if defined(_WIN32)

namespace kb::editor {

class Win32ErrorReporter {
public:
    Win32ErrorReporter() = delete;

    static void PrintLastError(const char* action) noexcept;
};

} // namespace kb::editor

#endif
