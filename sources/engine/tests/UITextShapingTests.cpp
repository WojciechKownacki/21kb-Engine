#include "engine/ui/visual/UITextShaping.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace {
void Require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

std::vector<kb::scene::UITextShapedGlyph> Shape(const std::vector<std::byte>& font, std::u32string_view text,
                                             std::size_t begin = 0U,
                                             std::size_t end = std::numeric_limits<std::size_t>::max()) {
    const std::vector<std::uint32_t> codepoints(text.begin(), text.end());
    std::vector<kb::scene::UITextShapedGlyph> glyphs;
    end = std::min(end, text.size());
    Require(kb::scene::ShapeUIText(font, codepoints, begin, end, 0.02F, glyphs), "Shaping failed");
    for (const auto& glyph : glyphs) {
        Require(glyph.glyphId != 0U, "Fixture font must cover every visible character");
        Require(glyph.clusterBegin >= begin && glyph.clusterEnd <= end && glyph.clusterBegin < glyph.clusterEnd,
                "Shaping must preserve logical cluster bounds");
        Require(std::isfinite(glyph.advance) && std::isfinite(glyph.offsetX) && std::isfinite(glyph.offsetY),
                "Glyph metrics must be finite");
    }
    return glyphs;
}
} // namespace

int main(int argc, char** argv) {
    try {
        Require(argc == 2, "Expected a font fixture path");
        std::ifstream stream(argv[1], std::ios::binary | std::ios::ate);
        Require(stream.good(), "Cannot open font fixture");
        const auto length = stream.tellg();
        Require(length > 0, "Font fixture is empty");
        std::vector<std::byte> font(static_cast<std::size_t>(length));
        stream.seekg(0);
        stream.read(reinterpret_cast<char*>(font.data()), static_cast<std::streamsize>(font.size()));
        Require(stream.good(), "Cannot read font fixture");

        const auto ligature = Shape(font, U"ffi");
        Require(ligature.size() == 1U && ligature[0].clusterBegin == 0U && ligature[0].clusterEnd == 3U,
                "Latin ligatures must retain all source characters");
        {
            const std::vector<std::uint32_t> styledText{U'f', U'f', U'i'};
            const std::vector<std::uint32_t> boundaries{1U};
            std::vector<kb::scene::UITextShapedGlyph> styled;
            Require(kb::scene::ShapeUIText(font, styledText, 0, 3, 0.02F, styled, boundaries) && styled.size() == 2U &&
                    styled[0].clusterEnd == 1U && styled[1].clusterBegin == 1U,
                    "Ligatures must not consume the next style's characters");
        }
        const auto accent = Shape(font, U"a\u0301");
        Require(accent.size() == 1U && accent[0].clusterEnd == 2U,
                "A combining accent must belong to the base cluster");
        const auto hebrew = Shape(font, U"\u05D0\u05D1\u05D2");
        Require(hebrew.size() == 3U && hebrew[0].clusterBegin == 2U && hebrew[2].clusterBegin == 0U &&
                std::all_of(hebrew.begin(), hebrew.end(), [](const auto& g) { return g.rightToLeft; }),
                "Hebrew must appear in visual RTL order");
        const auto arabic = Shape(font, U"\u0633\u0644\u0627\u0645");
        Require(arabic.size() == 3U && arabic.front().clusterBegin == 3U && arabic.back().clusterBegin == 0U,
                "Arabic must join contextually and form the lam-alef ligature");
        const auto isolated = Shape(font, U"\u0633");
        Require(arabic.back().glyphId != isolated[0].glyphId, "Arabic initial and isolated forms must differ");
        const auto wrappedArabic = Shape(font, U"\u0633\u0644\u0627\u0645", 0, 1);
        Require(wrappedArabic.size() == 1U && wrappedArabic[0].glyphId == isolated[0].glyphId,
                "Arabic joining must stop at a visual line boundary");
        const auto mixed = Shape(font, U"abc \u05D0\u05D1 123");
        std::vector<std::uint32_t> order;
        for (const auto& glyph : mixed) order.push_back(glyph.clusterBegin);
        Require(order == std::vector<std::uint32_t>{0, 1, 2, 3, 7, 8, 9, 6, 5, 4},
                "Mixed text must retain LTR numbers inside the RTL run");
        const auto wrapped = Shape(font, U"\u05D0\u05D1\u05D2 123 abc", 4, 11);
        Require(wrapped.front().clusterBegin == 8U && wrapped.back().clusterBegin == 6U,
                "A wrapped neutral/LTR line must inherit the original RTL paragraph base level");
        const auto secondParagraph = Shape(font, U"abc\n\u05D0\u05D1", 4, 6);
        Require(secondParagraph.front().clusterBegin == 5U && secondParagraph.back().clusterBegin == 4U,
                "Explicit paragraphs must resolve direction independently");

        const std::vector<std::uint32_t> text{U'A'};
        std::vector<kb::scene::UITextShapedGlyph> output = ligature;
        Require(!kb::scene::ShapeUIText({}, text, 0, 1, 1.0F, output) && output.empty(),
                "Invalid fonts must clear previous output");
        Require(!kb::scene::ShapeUIText(font, text, 0, 2, 1.0F, output), "Invalid line ranges must fail");
        Require(!kb::scene::ShapeUIText(font, text, 0, 1, std::numeric_limits<float>::infinity(), output),
                "Nonfinite scale must fail");
        Require(kb::scene::ShapeUIText(font, text, 0, 0, 1.0F, output) && output.empty(),
                "An empty line must succeed without glyphs");
        for (const auto invalid : {0xD800U, 0xDFFFU, 0x110000U, 0xFFFFFFFFU}) {
            const std::vector<std::uint32_t> malformed{invalid};
            Require(!kb::scene::ShapeUIText(font, malformed, 0, 1, 1.0F, output) && output.empty(),
                    "Invalid Unicode scalars must fail without output");
        }
        Require(!kb::scene::ShapeUIText(font, text, 0, 1, std::numeric_limits<float>::max(), output) && output.empty(),
                "Overflowing scaled glyph metrics must fail without output");
        std::cout << "Text shaping: ligatures, combining marks, Arabic joining, mixed direction, line context and validation passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
