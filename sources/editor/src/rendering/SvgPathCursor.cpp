#include "rendering/SvgPathCursor.hpp"

#include <charconv>

namespace kb::editor {
namespace {

[[nodiscard]] bool IsCommand(char value) noexcept {
    switch (value) {
    case 'M':
    case 'm':
    case 'L':
    case 'l':
    case 'H':
    case 'h':
    case 'V':
    case 'v':
    case 'C':
    case 'c':
    case 'A':
    case 'a':
    case 'Z':
    case 'z':
        return true;
    default:
        return false;
    }
}

} // namespace

SvgPathCursor::SvgPathCursor(std::string_view text) : text_(text) {}

bool SvgPathCursor::HasMore() {
    SkipSeparators();
    return position_ < text_.size();
}

bool SvgPathCursor::NextIsCommand() {
    SkipSeparators();
    return position_ < text_.size() && IsCommand(text_[position_]);
}

char SvgPathCursor::ReadCommand() {
    SkipSeparators();
    return position_ < text_.size() ? text_[position_++] : '\0';
}

bool SvgPathCursor::ReadFlag(bool& output) {
    SkipSeparators();
    if (position_ >= text_.size()) {
        return false;
    }
    const char value = text_[position_];
    if (value != '0' && value != '1') {
        return false;
    }
    output = value == '1';
    ++position_;
    return true;
}

bool SvgPathCursor::ReadNumber(double& output) {
    SkipSeparators();
    if (position_ >= text_.size()) {
        return false;
    }

    const char* first = text_.data() + position_;
    const char* last = text_.data() + text_.size();
    const std::from_chars_result result = std::from_chars(first, last, output);
    if (result.ec != std::errc{}) {
        return false;
    }

    position_ = static_cast<std::size_t>(result.ptr - text_.data());
    return true;
}

void SvgPathCursor::SkipSeparators() {
    while (position_ < text_.size()) {
        const char c = text_[position_];
        if (c == ',' || c == ' ' || c == '\n' || c == '\r' || c == '\t') {
            ++position_;
            continue;
        }
        break;
    }
}

} // namespace kb::editor
