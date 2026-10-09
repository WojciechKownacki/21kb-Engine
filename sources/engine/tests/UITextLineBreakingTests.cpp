#include "TestSupport.hpp"
#include "TestSuites.hpp"

#include "engine/ui/visual/UITextLineBreaking.hpp"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace kb::tests {
namespace {

// Lays out `text` with every character one unit wide and returns each line's characters.
[[nodiscard]] std::vector<std::u32string> Break(std::u32string_view text, kb::scene::UITextWrapMode mode, float width,
                                                std::vector<kb::scene::UITextLineSpan>* spans = nullptr) {
    const std::vector<std::uint32_t> codepoints(text.begin(), text.end());
    std::vector<float> advances;
    for (const char32_t character : text) advances.push_back(character == U'\n' ? 0.0F : 1.0F);
    std::vector<kb::scene::UITextLineSpan> lines;
    kb::scene::BreakUITextLines(codepoints, advances, mode, width, 0.0F, lines);
    std::vector<std::u32string> result;
    for (const kb::scene::UITextLineSpan& line : lines) {
        result.emplace_back(text.substr(line.begin, line.end - line.begin));
    }
    if (spans != nullptr) *spans = lines;
    return result;
}

void TestExplicitLineBreaksSurviveWordWrap() {
    using kb::scene::UITextWrapMode;
    // The word wrap pass used to move the last word of a line ended by '\n' onto the next line, merging
    // the two authored lines ("Hello" + "worldFoo").
    std::vector<kb::scene::UITextLineSpan> spans;
    const auto wrapped = Break(U"Hello world\nFoo", UITextWrapMode::Word, 20.0F, &spans);
    Require(wrapped == std::vector<std::u32string>{ U"Hello world", U"Foo" },
        "Word wrap must keep an authored line break when the line before it fits");
    Require(spans.size() == 2U && spans[0].hardBreak && !spans[1].hardBreak && spans[0].width == 11.0F,
        "An authored line break must mark its line and leave its width unchanged");

    const auto narrow = Break(U"one two three\nfour five", UITextWrapMode::Word, 9.0F);
    Require(narrow == std::vector<std::u32string>{ U"one two", U"three", U"four five" },
        "Word wrap inside a paragraph must not carry words across the authored line break");

    Require(Break(U"a\n\nb", UITextWrapMode::Word, 10.0F) == std::vector<std::u32string>{ U"a", U"", U"b" },
        "Consecutive authored line breaks must keep the empty line between them");
    Require(Break(U"a\nb", UITextWrapMode::NoWrap, 0.5F) == std::vector<std::u32string>{ U"a", U"b" },
        "NoWrap text must still honour authored line breaks");
    Require(Break(U"ab\ncd", UITextWrapMode::Character, 10.0F) == std::vector<std::u32string>{ U"ab", U"cd" },
        "Character wrap must honour authored line breaks");
    Require(Break(U"end\n", UITextWrapMode::Word, 10.0F) == std::vector<std::u32string>{ U"end", U"" },
        "A trailing authored line break must open an empty last line");
}

void TestWordWrapBreaksAtSpacesAndSplitsLongWords() {
    using kb::scene::UITextWrapMode;
    std::vector<kb::scene::UITextLineSpan> spans;
    Require(Break(U"aa bb cc", UITextWrapMode::Word, 5.0F, &spans) == std::vector<std::u32string>{ U"aa bb", U"cc" },
        "Word wrap must break at the last space that fits");
    Require(Break(U"aa bb", UITextWrapMode::Word, 3.0F, &spans) == std::vector<std::u32string>{ U"aa", U"bb" } &&
            spans[0].width == 2.0F,
        "The space at a wrap point must not count toward the line width");
    Require(Break(U"abcdefgh", UITextWrapMode::Word, 3.0F) == std::vector<std::u32string>{ U"abc", U"def", U"gh" },
        "A word wider than the line must be split where it overflows");
    Require(Break(U"ab abcdefgh", UITextWrapMode::Word, 4.0F) == std::vector<std::u32string>{ U"ab", U"abcd", U"efgh" },
        "A long word after a space must move to its own line before it is split");
    Require(Break(U"abcd", UITextWrapMode::NoWrap, 1.0F) == std::vector<std::u32string>{ U"abcd" },
        "NoWrap must never wrap");
    Require(Break(U"abcd", UITextWrapMode::Character, 3.0F) == std::vector<std::u32string>{ U"abc", U"d" },
        "Character wrap must break between any two characters");
    Require(Break(U"", UITextWrapMode::Word, 3.0F) == std::vector<std::u32string>{ U"" },
        "Empty text lays out as one empty line");
}

void TestCjkTextWrapsBetweenIdeographsWithKinsoku() {
    using kb::scene::UITextWrapMode;
    // Japanese, Chinese and Korean text without spaces used to stay on one line in Word mode.
    Require(Break(U"\u65E5\u672C\u8A9E\u306E\u6587\u7AE0\u3067\u3059", UITextWrapMode::Word, 3.0F) ==
                std::vector<std::u32string>{ U"\u65E5\u672C\u8A9E", U"\u306E\u6587\u7AE0", U"\u3067\u3059" },
        "Japanese text must wrap between characters in Word mode");
    Require(Break(U"\u6211\u4EEC\u4ECA\u5929\u53BB\u5B66\u6821", UITextWrapMode::Word, 4.0F) ==
                std::vector<std::u32string>{ U"\u6211\u4EEC\u4ECA\u5929", U"\u53BB\u5B66\u6821" },
        "Chinese text must wrap between ideographs in Word mode");
    Require(Break(U"\uC548\uB155\uD558\uC138\uC694", UITextWrapMode::Word, 2.0F) ==
                std::vector<std::u32string>{ U"\uC548\uB155", U"\uD558\uC138", U"\uC694" },
        "Hangul syllables must allow a wrap between them");

    // Kinsoku: an ideographic full stop may not start a line, so the ideograph before it moves down with it.
    Require(Break(U"\u4ECA\u65E5\u306F\u6674\u308C\u3002\u660E\u65E5", UITextWrapMode::Word, 5.0F) ==
                std::vector<std::u32string>{ U"\u4ECA\u65E5\u306F\u6674", U"\u308C\u3002\u660E\u65E5" },
        "A line must not start with an ideographic full stop");
    Require(Break(U"\u3053\u308C\u306F\u3001\u30C6\u30B9\u30C8", UITextWrapMode::Word, 3.0F) ==
                std::vector<std::u32string>{ U"\u3053\u308C", U"\u306F\u3001\u30C6", U"\u30B9\u30C8" },
        "A line must not start with an ideographic comma");
    Require(Break(U"\u5F7C\u306F\u300C\u672C\u300D\u3068\u8A00\u3063\u305F", UITextWrapMode::Word, 3.0F) ==
                std::vector<std::u32string>{ U"\u5F7C\u306F", U"\u300C\u672C\u300D", U"\u3068\u8A00\u3063", U"\u305F" },
        "Opening brackets must not end a line and closing brackets must not start one");
    Require(Break(U"\u3061\u3087\u3063\u3068", UITextWrapMode::Word, 2.0F) ==
                std::vector<std::u32string>{ U"\u3061", U"\u3087\u3063", U"\u3068" },
        "Small kana must not start a line while the line can break elsewhere");
    Require(Break(U"\u30AB\u30FC\u30C9\u30B2\u30FC\u30E0", UITextWrapMode::Word, 2.0F) ==
                std::vector<std::u32string>{ U"\u30AB\u30FC", U"\u30C9", U"\u30B2\u30FC", U"\u30E0" },
        "The prolonged sound mark must not start a line");
    Require(Break(U"\u7D42\u308F\u308A\u3002\u300D", UITextWrapMode::Character, 3.0F) ==
                std::vector<std::u32string>{ U"\u7D42\u308F", U"\u308A\u3002\u300D" },
        "Character wrap must keep the kinsoku rules where another break exists");

    // Mixed scripts: Latin words still wrap only at spaces, ideographs on either side may wrap.
    Require(Break(U"Play \u30B2\u30FC\u30E0 now", UITextWrapMode::Word, 7.0F) ==
                std::vector<std::u32string>{ U"Play \u30B2\u30FC", U"\u30E0 now" },
        "Mixed Latin and CJK text must wrap at spaces and between ideographs");
    Require(Break(U"Hello\u4E16\u754C", UITextWrapMode::Word, 6.0F) == std::vector<std::u32string>{ U"Hello\u4E16", U"\u754C" },
        "A Latin word may wrap before an ideograph that follows it");
    Require(Break(U"a (b", UITextWrapMode::Word, 3.0F) == std::vector<std::u32string>{ U"a", U"(b" },
        "An opening parenthesis stays with the word it opens");
    Require(Break(U"aa bb!", UITextWrapMode::Word, 5.0F) == std::vector<std::u32string>{ U"aa", U"bb!" },
        "Closing punctuation stays on the line of the word before it");

    Require(kb::scene::IsUITextBreakableIdeographic(0x4E00U) && kb::scene::IsUITextBreakableIdeographic(0x3042U) &&
            kb::scene::IsUITextBreakableIdeographic(0xAC00U) && kb::scene::IsUITextBreakableIdeographic(0x20000U) &&
            !kb::scene::IsUITextBreakableIdeographic(U'a') && !kb::scene::IsUITextBreakableIdeographic(0x00E9U),
        "Ideographic classification must cover CJK, kana, hangul and the supplementary ideograph planes only");
    Require(kb::scene::IsUITextLineStartProhibited(0x3002U) && kb::scene::IsUITextLineStartProhibited(0xFF09U) &&
            kb::scene::IsUITextLineStartProhibited(0x30C3U) && !kb::scene::IsUITextLineStartProhibited(0x300CU) &&
            kb::scene::IsUITextLineEndProhibited(0x300CU) && kb::scene::IsUITextLineEndProhibited(0xFF08U) &&
            !kb::scene::IsUITextLineEndProhibited(0x300DU),
        "Kinsoku tables must classify brackets, stops and small kana");
}

} // namespace

void RunUITextLineBreakingTests() {
    TestExplicitLineBreaksSurviveWordWrap();
    TestWordWrapBreaksAtSpacesAndSplitsLongWords();
    TestCjkTextWrapsBetweenIdeographsWithKinsoku();
}

} // namespace kb::tests
