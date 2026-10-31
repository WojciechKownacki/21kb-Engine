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
        Require(argc == 4, "Expected text, GDEF caret and complex-script font fixture paths");
        std::ifstream stream(argv[1], std::ios::binary | std::ios::ate);
        Require(stream.good(), "Cannot open font fixture");
        const auto length = stream.tellg();
        Require(length > 0, "Font fixture is empty");
        std::vector<std::byte> font(static_cast<std::size_t>(length));
        stream.seekg(0);
        stream.read(reinterpret_cast<char*>(font.data()), static_cast<std::streamsize>(font.size()));
        Require(stream.good(), "Cannot read font fixture");
        {
            std::ifstream caretStream(argv[2], std::ios::binary | std::ios::ate);
            Require(caretStream.good() && caretStream.tellg() > 0, "Cannot read the caret font fixture");
            std::vector<std::byte> caretFont(static_cast<std::size_t>(caretStream.tellg()));
            caretStream.seekg(0);
            caretStream.read(reinterpret_cast<char*>(caretFont.data()), static_cast<std::streamsize>(caretFont.size()));
            const auto caretGlyphs = Shape(caretFont, U"fi");
            Require(caretGlyphs.size() == 1U && caretGlyphs[0].ligatureCarets.size() == 1U &&
                std::abs(caretGlyphs[0].ligatureCarets[0] - 4.0F) < 0.001F &&
                std::abs(caretGlyphs[0].advance - 20.0F) < 0.001F,
                "GDEF carets must retain the font's asymmetric component position, not half the advance");
        }

        const auto ligature = Shape(font, U"ffi");
        {
            std::ifstream complexStream(argv[3], std::ios::binary | std::ios::ate);
            Require(complexStream.good() && complexStream.tellg() > 0, "Cannot read complex-script font fixture");
            std::vector<std::byte> complexFont(static_cast<std::size_t>(complexStream.tellg()));
            complexStream.seekg(0);
            complexStream.read(reinterpret_cast<char*>(complexFont.data()), static_cast<std::streamsize>(complexFont.size()));
            const auto matra = Shape(complexFont, U"\u0915\u093F");
            Require(matra.size() == 2U && matra[0].glyphId == 545U && matra[1].glyphId == 56U &&
                matra[0].clusterBegin == 0U && matra[1].clusterEnd == 2U,
                "Devanagari pre-base vowel shaping must select and reorder the contextual matra");
            const auto conjunct = Shape(complexFont, U"\u0915\u094D\u0937\u093F");
            Require(conjunct.size() == 2U && conjunct[0].glyphId == 551U && conjunct[1].glyphId == 90U &&
                conjunct[0].clusterBegin == 0U && conjunct[1].clusterEnd == 4U,
                "Devanagari conjuncts must substitute the joined form and preserve the logical syllable");
            const std::vector<std::uint32_t> mixedText{U'f', U'f', U'i', U' ', 0x0915U, 0x093FU};
            const std::vector<std::uint32_t> graphemes{0U, 1U, 2U, 3U, 4U, 6U};
            std::vector<kb::scene::UITextFontShapedGlyph> mixedFonts;
            Require(kb::scene::ShapeUITextWithFallback(font, complexFont, mixedText, 0U, mixedText.size(),
                0.02F, 0.02F, graphemes, mixedFonts), "Mixed-font shaping failed");
            Require(!mixedFonts.empty() && mixedFonts.front().glyph.clusterBegin == 0U &&
                mixedFonts.front().glyph.clusterEnd == 3U && !mixedFonts.front().fallback &&
                std::ranges::count_if(mixedFonts, [](const auto& g) { return g.fallback; }) == 2 &&
                std::ranges::none_of(mixedFonts, [](const auto& g) { return g.glyph.glyphId == 0U; }),
                "Fallback must preserve the primary ligature and shape the Devanagari grapheme in one font");
            const std::vector<std::uint32_t> rtlText{0x0915U, U' ', U'a', U'b', U' ', 0x05D0U, 0x05D1U};
            const std::vector<std::uint32_t> rtlBoundaries{0U, 1U, 2U, 3U, 4U, 5U, 6U, 7U};
            Require(kb::scene::ShapeUITextWithFallback(complexFont, font, rtlText, 0U, rtlText.size(),
                0.02F, 0.02F, rtlBoundaries, mixedFonts) &&
                std::ranges::none_of(mixedFonts, [](const auto& g) { return g.glyph.glyphId == 0U; }),
                "Mixed-direction fallback text must retain covered glyphs");
            const auto hebrewFirst = std::ranges::find_if(mixedFonts, [](const auto& g) { return g.glyph.clusterBegin == 6U; });
            const auto hebrewLast = std::ranges::find_if(mixedFonts, [](const auto& g) { return g.glyph.clusterBegin == 5U; });
            Require(hebrewFirst != mixedFonts.end() && hebrewLast != mixedFonts.end() && hebrewFirst < hebrewLast &&
                hebrewFirst->fallback && hebrewLast->fallback, "Fallback must retain visual RTL glyph order");
            const std::vector<std::uint32_t> unknown{0x10FFFFU};
            const std::vector<std::uint32_t> unknownBoundaries{0U, 1U};
            Require(kb::scene::ShapeUITextWithFallback(font, complexFont, unknown, 0U, 1U, 0.02F, 0.02F,
                unknownBoundaries, mixedFonts) && mixedFonts.size() == 1U && mixedFonts[0].glyph.glyphId == 0U &&
                !mixedFonts[0].fallback, "A grapheme absent from both fonts must use the primary missing glyph");
        }
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
        {
            const std::vector<std::uint32_t> text{0x0431U};
            const std::vector<std::uint32_t> boundaries{0U, 1U};
            std::vector<kb::scene::UITextFontShapedGlyph> serbian, russian;
            Require(kb::scene::ShapeUITextWithFallback(font, {}, text, 0U, 1U, 0.02F, 0.0F, boundaries,
                        serbian, {}, {}, "sr") &&
                    kb::scene::ShapeUITextWithFallback(font, {}, text, 0U, 1U, 0.02F, 0.0F, boundaries,
                        russian, {}, {}, "ru") && serbian.size() == 1U && russian.size() == 1U &&
                    serbian[0].glyph.glyphId != russian[0].glyph.glyphId,
                    "Locale-sensitive OpenType substitutions must follow the selected language");
        }
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
        std::vector<kb::scene::UITextFontVariation> axes;
        Require(kb::scene::ParseUITextFontVariations("wght=450,wdth=90", axes) && axes.size() == 2U &&
                axes[0].tag == 0x77676874U && axes[0].value == 450.0F && axes[1].value == 90.0F,
                "Variable font settings must retain their tags and values");
        for (const std::string_view invalid : {"wght=", "wght=nan", "wght=1,", "wght=1,wght=2", "bad=1"})
            Require(!kb::scene::ParseUITextFontVariations(invalid, axes) && axes.empty(),
                    "Malformed variable font settings must fail without partial output");
        Require(!kb::scene::ShapeUIText({}, text, 0, 1, 1.0F, output) && output.empty(),
                "Invalid fonts must clear previous output");
        Require(!kb::scene::ShapeUIText(font, text, 0, 2, 1.0F, output), "Invalid line ranges must fail");
        Require(!kb::scene::ShapeUIText(font, text, 0, 1, std::numeric_limits<float>::infinity(), output),
                "Nonfinite scale must fail");
        const std::vector<kb::scene::UITextFontVariation> nonexistentAxis{{0x77676874U, 400.0F}};
        Require(!kb::scene::ShapeUIText(font, text, 0, 1, 1.0F, output, {}, nonexistentAxis) && output.empty(),
                "An axis absent from the face must fail without producing glyphs");
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
