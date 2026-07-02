#pragma once

#include <cstdint>
#include <string_view>

namespace kb::editor {

class EditorCrashBreadcrumbs {
public:
    EditorCrashBreadcrumbs() = delete;

    static void Reset();
    static void Write(std::string_view category, std::string_view message);
    static void WriteValue(std::string_view category, std::string_view label, std::uint64_t value);
    static void InstallUnhandledExceptionLogger();
};

} // namespace kb::editor
