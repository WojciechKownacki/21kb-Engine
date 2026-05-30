#include "platform/win32/Win32ErrorReporter.hpp"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>

#include <cstdio>

namespace kb::editor {

void Win32ErrorReporter::PrintLastError(const char* action) noexcept {
    const DWORD error = GetLastError();
    char message[512]{};
    FormatMessageA(
        FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr,
        error,
        MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        message,
        static_cast<DWORD>(sizeof(message)),
        nullptr);

    std::fprintf(stderr, "%s failed. Win32 error %lu: %s\n", action, error, message);
}

} // namespace kb::editor

#endif
