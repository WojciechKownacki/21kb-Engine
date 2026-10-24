#include <cstdio>
#include <cstdlib>
#include <initializer_list>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <crtdbg.h>

// Configure CRT reporting before ordinary test globals can fail during construction.
#pragma warning(push)
#pragma warning(disable: 4073) // Library initialization priority is intentional for this test support.
#pragma init_seg(lib)
#pragma warning(pop)

namespace {
#if defined(_DEBUG)
int __cdecl FatalReport(int kind, char* message, int*) {
    if (kind == _CRT_ASSERT || kind == _CRT_ERROR) {
        std::fputs(message != nullptr ? message : "Fatal CRT test error\n", stderr);
        std::fflush(stderr);
        std::_Exit(EXIT_FAILURE);
    }
    return FALSE;
}

int __cdecl FatalWideReport(int kind, wchar_t* message, int*) {
    if (kind == _CRT_ASSERT || kind == _CRT_ERROR) {
        char text[16384]{};
        if (message != nullptr && WideCharToMultiByte(CP_UTF8, 0, message, -1, text, sizeof(text), nullptr, nullptr) > 0) {
            std::fputs(text, stderr);
        } else {
            std::fputs("Fatal CRT test error\n", stderr);
        }
        std::fflush(stderr);
        std::_Exit(EXIT_FAILURE);
    }
    return FALSE;
}
#endif

struct TestProcessErrors {
    TestProcessErrors() noexcept {
        SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
        static_cast<void>(_set_error_mode(_OUT_TO_STDERR));
        static_cast<void>(_set_abort_behavior(_WRITE_ABORT_MSG, _WRITE_ABORT_MSG | _CALL_REPORTFAULT));
        // Preserve diagnostics even when an assertion terminates before normal stream cleanup.
        static_cast<void>(std::setvbuf(stdout, nullptr, _IONBF, 0));
        static_cast<void>(std::setvbuf(stderr, nullptr, _IONBF, 0));
#if defined(_DEBUG)
        for (int kind : { _CRT_WARN, _CRT_ERROR, _CRT_ASSERT }) {
            static_cast<void>(_CrtSetReportMode(kind, _CRTDBG_MODE_FILE));
            static_cast<void>(_CrtSetReportFile(kind, _CRTDBG_FILE_STDERR));
        }
        // A CRT assertion must fail the process rather than silently continue after reporting.
        static_cast<void>(_CrtSetReportHook2(_CRT_RPTHOOK_INSTALL, &FatalReport));
        static_cast<void>(_CrtSetReportHookW2(_CRT_RPTHOOK_INSTALL, &FatalWideReport));
#endif
    }
};

[[maybe_unused]] const TestProcessErrors configured;
} // namespace
