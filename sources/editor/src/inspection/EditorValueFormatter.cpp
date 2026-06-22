#include "inspection/EditorValueFormatter.hpp"

#include <cstdio>

namespace kb::editor {

std::string EditorValueFormatter::FormatFloat(float value, int precision) {
    char buffer[32]{};
    std::snprintf(buffer, sizeof(buffer),
        precision == 0 ? "%.0f" : precision == 1 ? "%.1f" : precision == 2 ? "%.2f" : "%.3f",
        static_cast<double>(value));
    std::string text = buffer;
    if (text.find('.') != std::string::npos) {
        while (!text.empty() && text.back() == '0') {
            text.pop_back();
        }
        if (!text.empty() && text.back() == '.') {
            text.pop_back();
        }
    }
    return text == "-0" ? "0" : text;
}

std::string EditorValueFormatter::FormatUInt64(std::uint64_t value) {
    char buffer[32]{};
    std::snprintf(buffer, sizeof(buffer), "%llu", static_cast<unsigned long long>(value));
    return buffer;
}

} // namespace kb::editor
