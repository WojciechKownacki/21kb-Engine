// Installs the crash reporter and then fails in the way the first argument names.
// Driven by CrashReportingTests.cpp; a console program, so nothing ever opens a window.

#include "engine/platform/CrashReporting.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <crtdbg.h>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

volatile int g_sink = 0;
volatile bool g_keepRecursing = true;

__declspec(noinline) int Recurse(int depth) {
    volatile char frame[1024];
    frame[0] = static_cast<char>(depth);
    if (g_keepRecursing) {
        g_sink = Recurse(depth + 1) + frame[0];
    }
    return g_sink;
}

struct Base {
    Base() { CallFromConstructor(); }
    virtual ~Base() = default;
    Base(const Base&) = delete;
    Base& operator=(const Base&) = delete;
    __declspec(noinline) void CallFromConstructor() { Pure(); }
    virtual void Pure() = 0;
};

struct Derived final : Base {
    void Pure() override { g_sink = 1; }
};

} // namespace

int wmain(int argc, wchar_t** argv) {
    if (argc < 3) {
        std::fputs("usage: kb_crash_reporting_test_helper <report directory> <mode>\n", stderr);
        return 2;
    }
    const std::wstring_view mode{ argv[2] };
#if defined(_DEBUG)
    // A debug CRT asserts before it reports an invalid parameter; keep that
    // assertion out of a dialog so the reporter is what the test observes.
    _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_DEBUG);
    _CrtSetReportMode(_CRT_ERROR, _CRTDBG_MODE_DEBUG);
#endif
    kb::platform::CrashReporterOptions options;
    options.productName = "Crash Test Helper";
    options.version = "1.2.3";
    options.reportDirectory = argv[1];
    options.uploadPendingReports = false;

    const auto installBegin = std::chrono::steady_clock::now();
    if (!kb::platform::CrashReporter::Install(options)) {
        return 4;
    }
    const auto installEnd = std::chrono::steady_clock::now();
    if (kb::platform::CrashReporter::Install(options)) {
        return 5;
    }

    kb::platform::CrashReporter::Note("helper: before the crash");
    wchar_t profile[1024];
    const DWORD profileLength = GetEnvironmentVariableW(L"USERPROFILE", profile, 1024U);
    if (profileLength != 0U && profileLength < 1024U) {
        char narrow[2048];
        const int written = WideCharToMultiByte(CP_UTF8, 0, profile, -1, narrow, sizeof(narrow), nullptr, nullptr);
        if (written > 0) {
            const std::string line = std::string{ "helper: opened " } + narrow + "\\Documents\\save.dat";
            kb::platform::CrashReporter::Note(line);
        }
    }

    if (mode == L"timing") {
        // Install cost on the starting thread and the cost of one Note, which
        // the editor makes on its per-frame breadcrumb path.
        constexpr int kNotes = 1'000'000;
        const auto notesBegin = std::chrono::steady_clock::now();
        for (int index = 0; index < kNotes; ++index) {
            kb::platform::CrashReporter::Note("frame: paint scene viewport and submit draw lists");
        }
        const auto notesEnd = std::chrono::steady_clock::now();
        std::printf("install_us=%lld note_ns=%.2f\n",
            static_cast<long long>(std::chrono::duration_cast<std::chrono::microseconds>(installEnd - installBegin).count()),
            static_cast<double>(std::chrono::duration_cast<std::chrono::nanoseconds>(notesEnd - notesBegin).count()) / kNotes);
        return 0;
    }
    // Let the reporter thread finish starting, as it has long before any real crash.
    Sleep(200U);
    if (mode == L"access-violation") {
        volatile int* pointer = nullptr;
        *pointer = 42;
    } else if (mode == L"stack-overflow") {
        g_sink = Recurse(0);
    } else if (mode == L"terminate") {
        try {
            throw std::runtime_error("helper terminate reason");
        } catch (...) {
            std::terminate();
        }
    } else if (mode == L"abort") {
        std::abort();
    } else if (mode == L"pure-call") {
        Derived derived;
        g_sink = 2;
    } else if (mode == L"invalid-parameter") {
        char destination[2];
        const char* volatile source = "too long for the destination";
        strcpy_s(destination, sizeof(destination), source);
        g_sink = destination[0];
    } else if (mode == L"thread-abort") {
        // abort() on a thread other than the one that installed the reporter.
        HANDLE thread = CreateThread(nullptr, 0U, [](void*) -> DWORD { std::abort(); }, nullptr, 0U, nullptr);
        if (thread != nullptr) {
            WaitForSingleObject(thread, INFINITE);
        }
    } else {
        return 3;
    }
    return 0;
}
