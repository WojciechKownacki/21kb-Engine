#include "project/ProjectDescriptorFieldCodec.hpp"

namespace kb::project {

std::string ProjectDescriptorFieldCodec::Escape(std::string_view value) {
    std::string escaped;
    escaped.reserve(value.size());
    for (const char character : value) {
        switch (character) {
        case '\\':
            escaped += "\\\\";
            break;
        case '\n':
            escaped += "\\n";
            break;
        case '\r':
            escaped += "\\r";
            break;
        case '\t':
            escaped += "\\t";
            break;
        default:
            escaped.push_back(character);
            break;
        }
    }
    return escaped;
}

std::optional<std::string> ProjectDescriptorFieldCodec::Unescape(std::string_view value) {
    std::string output;
    output.reserve(value.size());
    for (std::size_t index = 0; index < value.size(); ++index) {
        const char character = value[index];
        if (character != '\\') {
            output.push_back(character);
            continue;
        }
        if (++index >= value.size()) {
            return std::nullopt;
        }
        switch (value[index]) {
        case '\\':
            output.push_back('\\');
            break;
        case 'n':
            output.push_back('\n');
            break;
        case 'r':
            output.push_back('\r');
            break;
        case 't':
            output.push_back('\t');
            break;
        default:
            return std::nullopt;
        }
    }
    return output;
}

} // namespace kb::project
