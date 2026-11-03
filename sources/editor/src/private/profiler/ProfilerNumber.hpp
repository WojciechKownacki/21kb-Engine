#pragma once

#include <string>
#include <string_view>

namespace kb::editor {

// The numbers the profiler shows, written the way the editor's language writes them: Polish, German
// and most of Europe use a decimal comma. Files and anything a machine reads (the CSV, the settings)
// keep the point; only what a person reads goes through here.
struct ProfilerNumber final {
    ProfilerNumber() = delete;

    // Like "{:.Nf}": `decimals` digits after the separator.
    [[nodiscard]] static std::string Fixed(double value, int decimals);
    // Like "{:g}": the shortest form that keeps the value.
    [[nodiscard]] static std::string General(double value);
    // A number written with a point, in the separator of `language` (a code of the Language list).
    [[nodiscard]] static std::string Localized(std::string text, std::string_view language);
    [[nodiscard]] static bool UsesDecimalComma(std::string_view language) noexcept;
};

} // namespace kb::editor
