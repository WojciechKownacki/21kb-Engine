#include "engine/ui/visual/UITextGraphemes.hpp"

#include <algorithm>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
void Append(std::string& text, std::uint32_t scalar) {
    if (scalar <= 0x7FU) text.push_back(static_cast<char>(scalar));
    else {
        const unsigned count = scalar <= 0x7FFU ? 2U : scalar <= 0xFFFFU ? 3U : 4U;
        text.push_back(static_cast<char>((0xFFU << (8U - count)) | (scalar >> (6U * (count - 1U)))));
        for (unsigned index = count - 1U; index > 0U; --index)
            text.push_back(static_cast<char>(0x80U | ((scalar >> (6U * (index - 1U))) & 0x3FU)));
    }
}

void Check(const std::string& text, const std::vector<std::size_t>& boundaries) {
    using namespace kb::scene;
    if (boundaries.empty() || boundaries.front() != 0U || boundaries.back() != text.size() ||
        UITextGraphemeCount(text) != boundaries.size() - 1U)
        throw std::runtime_error("Cluster count mismatch");
    for (std::size_t offset = 0U; offset <= text.size() + 2U; ++offset) {
        const auto clamped = std::min(offset, text.size());
        const auto upper = std::upper_bound(boundaries.begin(), boundaries.end(), clamped);
        const auto lower = std::lower_bound(boundaries.begin(), boundaries.end(), clamped);
        const auto snap = *(upper - 1);
        const auto next = upper == boundaries.end() ? text.size() : *upper;
        const auto previous = lower == boundaries.begin() ? 0U : *(lower - 1);
        if (SnapToUITextGraphemeBoundary(text, offset) != snap ||
            NextUITextGraphemeBoundary(text, offset) != next ||
            PreviousUITextGraphemeBoundary(text, offset) != previous)
            throw std::runtime_error("Boundary mismatch at byte " + std::to_string(offset));
    }
}
} // namespace

int main(int argc, char** argv) {
    std::size_t lineNumber = 0U;
    try {
        if (argc != 2) throw std::runtime_error("Expected the Unicode GraphemeBreakTest.txt path");
        std::ifstream input(argv[1]);
        if (!input) throw std::runtime_error("Cannot read conformance corpus");
        std::string line;
        std::size_t cases = 0U;
        while (std::getline(input, line)) {
            ++lineNumber;
            line = line.substr(0U, line.find('#'));
            std::istringstream fields(line);
            std::string token;
            std::string text;
            std::vector<std::size_t> boundaries;
            while (fields >> token) {
                if (token == "\xC3\xB7") boundaries.push_back(text.size());
                else if (token != "\xC3\x97") Append(text, static_cast<std::uint32_t>(std::stoul(token, nullptr, 16)));
            }
            if (boundaries.empty()) continue;
            Check(text, boundaries);
            ++cases;
        }
        if (cases < 700U) throw std::runtime_error("Conformance corpus is incomplete");
        Check("", {0U});
        Check(std::string("a\0b", 3U), {0U, 1U, 2U, 3U});
        for (const auto& malformed : {std::string("\xC0\xAF"), std::string("\xED\xA0\x80"),
                 std::string("\xF4\x90\x80\x80"), std::string("\xF0\x9F"), std::string("\x80")}) {
            std::vector<std::size_t> boundaries;
            for (std::size_t offset = 0U; offset <= malformed.size(); ++offset) boundaries.push_back(offset);
            Check(malformed, boundaries);
        }
        std::cout << "Unicode 17.0.0: " << cases << " corpus cases; all byte offsets and malformed UTF-8 passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Conformance line " << lineNumber << ": " << error.what() << '\n';
        return 1;
    }
}
