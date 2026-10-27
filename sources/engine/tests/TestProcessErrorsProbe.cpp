// Exercise the real standard assertion path in every build configuration.
#undef NDEBUG
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <string_view>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <crtdbg.h>

#if defined(NDEBUG)
#error The process error probe requires active standard assertions
#endif

namespace {
struct GlobalAssertionProbe {
    GlobalAssertionProbe() {
        if (std::wstring_view{ GetCommandLineW() }.ends_with(L" global-assert")) {
            std::fputs("process error probe entered\n", stdout);
            assert(false && "global assertion probe");
        }
    }
};
[[maybe_unused]] const GlobalAssertionProbe globalProbe;
} // namespace

int main(int argc, char** argv) {
    if (argc != 2) return 2;
    const std::string_view mode{ argv[1] };
    if (mode == "capabilities") {
#if defined(_DEBUG)
        std::puts("debug-crt=1");
#else
        std::puts("debug-crt=0");
#endif
        return EXIT_SUCCESS;
    }
    std::fputs("process error probe entered\n", stdout);
    if (mode == "abort") std::abort();
    if (mode == "assert") assert(false && "standard assertion probe");
#if defined(_DEBUG)
    if (mode == "crt-assert") {
        static_cast<void>(_CrtDbgReportW(_CRT_ASSERT, _CRT_WIDE(__FILE__), __LINE__, nullptr, L"CRT assertion probe"));
        return EXIT_SUCCESS; // The process policy must prevent this return.
    }
#endif
    if (mode == "dialog-control") {
        // The negative control is allowed to create a window only on the test's private desktop.
        wchar_t desktopName[128]{};
        DWORD required = 0U;
        if (GetUserObjectInformationW(GetThreadDesktop(GetCurrentThreadId()), UOI_NAME,
                desktopName, sizeof(desktopName), &required) == FALSE ||
            !std::wstring_view{ desktopName }.starts_with(L"21kb-test-errors-")) return 2;
        static_cast<void>(MessageBoxW(nullptr, L"Hidden process error control", L"Process error control", MB_OK));
    }
    return 2;
}
