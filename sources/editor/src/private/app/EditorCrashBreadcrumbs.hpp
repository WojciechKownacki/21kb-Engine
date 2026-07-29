#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace kb::editor {

class EditorCrashBreadcrumbs {
public:
    EditorCrashBreadcrumbs() = delete;

    static void Reset();
    static void Write(std::string_view category, std::string_view message);
    static void WriteValue(std::string_view category, std::string_view label, std::uint64_t value);
    static void ConfigureCrashReport(
        std::string apiVersion,
        std::string apiHash,
        std::vector<std::pair<std::uint64_t, std::string>> assets);
    static void WriteCrashReport(std::string_view errorKind) noexcept;
    static void InstallUnhandledExceptionLogger();
};

} // namespace kb::editor
