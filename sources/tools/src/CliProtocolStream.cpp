#include "CliProtocolStream.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cstddef>
#include <iostream>
#include <optional>
#include <string_view>

#if defined(_WIN32)
#include <fcntl.h>
#include <io.h>
#include <cstdio>
#endif

namespace kb::cli {
namespace {

constexpr std::size_t kMaxBodyBytes = 64U * 1024U * 1024U;

[[nodiscard]] std::string_view Trim(std::string_view text) {
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front())) != 0) text.remove_prefix(1U);
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back())) != 0) text.remove_suffix(1U);
    return text;
}

} // namespace

void UseBinaryStdio(const std::istream& in) {
#if defined(_WIN32)
    if (&in == &std::cin) {
        static_cast<void>(_setmode(_fileno(stdin), _O_BINARY));
        static_cast<void>(_setmode(_fileno(stdout), _O_BINARY));
    }
#else
    static_cast<void>(in);
#endif
}

FramedRead ReadFramedMessage(std::istream& in, std::string& body) {
    std::optional<std::size_t> length;
    bool sawHeader = false;
    std::string line;
    for (;;) {
        if (!std::getline(in, line)) return FramedRead::EndOfInput;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) {
            if (sawHeader) break;
            continue; // a blank line between frames
        }
        sawHeader = true;
        const std::size_t colon = line.find(':');
        if (colon == std::string::npos) continue;
        std::string name = line.substr(0U, colon);
        std::ranges::transform(name, name.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (name != "content-length") continue;
        const std::string_view value = Trim(std::string_view{ line }.substr(colon + 1U));
        std::size_t parsed = 0U;
        const auto result = std::from_chars(value.data(), value.data() + value.size(), parsed);
        if (result.ec == std::errc{} && result.ptr == value.data() + value.size()) length = parsed;
        else length.reset();
    }
    if (!length) return FramedRead::Malformed;
    if (*length > kMaxBodyBytes) {
        in.ignore(static_cast<std::streamsize>(*length));
        return in.good() ? FramedRead::Malformed : FramedRead::EndOfInput;
    }
    body.assign(*length, '\0');
    in.read(body.data(), static_cast<std::streamsize>(*length));
    return static_cast<std::size_t>(in.gcount()) == *length ? FramedRead::Message : FramedRead::EndOfInput;
}

void WriteFramedMessage(std::ostream& out, const kb::core::JsonValue& message) {
    const std::string text = message.Dump();
    out << "Content-Length: " << text.size() << "\r\n\r\n" << text;
    out.flush();
}

} // namespace kb::cli
