#include "TestSupport.hpp"
#include "TestSuites.hpp"

#include "engine/ui/visual/UITextGraphemes.hpp"
#include "engine/ui/visual/UITextLineBreaking.hpp"

#include <algorithm>
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

// UTF-8 text from code points, so the tests state what they mean instead of byte escapes.
[[nodiscard]] std::string Utf8(std::u32string_view text) {
    std::string result;
    for (const char32_t c : text) {
        const auto value = static_cast<std::uint32_t>(c);
        if (value < 0x80U) {
            result.push_back(static_cast<char>(value));
        } else if (value < 0x800U) {
            result.push_back(static_cast<char>(0xC0U | (value >> 6U)));
            result.push_back(static_cast<char>(0x80U | (value & 0x3FU)));
        } else if (value < 0x10000U) {
            result.push_back(static_cast<char>(0xE0U | (value >> 12U)));
            result.push_back(static_cast<char>(0x80U | ((value >> 6U) & 0x3FU)));
            result.push_back(static_cast<char>(0x80U | (value & 0x3FU)));
        } else {
            result.push_back(static_cast<char>(0xF0U | (value >> 18U)));
            result.push_back(static_cast<char>(0x80U | ((value >> 12U) & 0x3FU)));
            result.push_back(static_cast<char>(0x80U | ((value >> 6U) & 0x3FU)));
            result.push_back(static_cast<char>(0x80U | (value & 0x3FU)));
        }
    }
    return result;
}

// What a player sees as one character is one cluster: a letter with its accent, a thumb with its skin tone, a family
// of joined emoji, a flag, a Hangul syllable written as jamo; and a boundary never falls inside one.
void TestGraphemeClusters() {
    using namespace kb::scene;
    struct Case {
        const char* name;
        std::u32string text;
        std::size_t clusters;
    };
    const Case cases[] = {
        { "plain letters", U"abc", 3U },
        { "precomposed accent", U"\u00E9", 1U },
        { "letter and combining accent", U"e\u0301", 1U },
        { "letter with two combining marks", U"a\u0323\u0302", 1U },
        { "thumbs up with skin tone", U"\U0001F44D\U0001F3FD", 1U },
        { "family joined by zero width joiners", U"\U0001F468\u200D\U0001F469\u200D\U0001F467", 1U },
        { "heart with variation selector", U"\u2764\uFE0F", 1U },
        { "one flag", U"\U0001F1F5\U0001F1F1", 1U },
        { "two flags", U"\U0001F1F5\U0001F1F1\U0001F1E9\U0001F1EA", 2U },
        { "three regional indicators", U"\U0001F1F5\U0001F1F1\U0001F1E9", 2U },
        { "hangul jamo syllable", U"\u1112\u1161\u11AB", 1U },
        { "hangul precomposed syllable and a final consonant", U"\uD55C\u11AB", 1U },
        { "thai consonant with vowel mark", U"\u0E01\u0E35", 1U },
        { "devanagari consonant with vowel sign", U"\u0915\u093E", 1U },
        { "carriage return and line feed", U"\r\n", 1U },
        { "two line feeds", U"\n\n", 2U },
        { "emoji then letter", U"\U0001F600x", 2U },
        { "joiner without a pictograph after it", U"a\u200Db", 2U },
        { "keycap sequence", U"1\uFE0F\u20E3", 1U },
    };
    for (const Case& c : cases) {
        const std::string text = Utf8(c.text);
        const std::size_t clusters = UITextGraphemeCount(text);
        Require(clusters == c.clusters, (std::string{ "Grapheme count of " } + c.name + " is " + std::to_string(clusters)).c_str());
        // Walking forward and backward visits the same boundaries, and every boundary lies between code points.
        std::vector<std::size_t> forward{ 0U };
        for (std::size_t offset = 0U; offset < text.size();) {
            offset = NextUITextGraphemeBoundary(text, offset);
            forward.push_back(offset);
        }
        Require(forward.size() == c.clusters + 1U && forward.back() == text.size(), c.name);
        std::vector<std::size_t> backward{ text.size() };
        for (std::size_t offset = text.size(); offset > 0U;) {
            offset = PreviousUITextGraphemeBoundary(text, offset);
            backward.push_back(offset);
        }
        std::ranges::reverse(backward);
        Require(forward == backward, (std::string{ "Forward and backward boundaries differ for " } + c.name).c_str());
        // An offset in the middle of any cluster snaps back to its start.
        for (std::size_t index = 0U; index + 1U < forward.size(); ++index) {
            for (std::size_t offset = forward[index]; offset < forward[index + 1U]; ++offset) {
                Require(SnapToUITextGraphemeBoundary(text, offset) == forward[index], c.name);
            }
        }
    }
    Require(IsUITextGraphemeExtender(0x0301U) && IsUITextGraphemeExtender(0x200DU) && IsUITextGraphemeExtender(0xFE0FU) &&
            !IsUITextGraphemeExtender(U'a') && !IsUITextGraphemeExtender(0x1F600U),
        "Extender classification is wrong");

    // Invalid UTF-8 never crashes or loops: every byte of a broken sequence is a cluster of its own and the walk ends.
    std::uint32_t state = 0x9E3779B9U;
    for (int round = 0; round < 300; ++round) {
        std::string noise;
        const std::size_t length = static_cast<std::size_t>(round % 40);
        for (std::size_t index = 0U; index < length; ++index) {
            state = state * 1664525U + 1013904223U;
            noise.push_back(static_cast<char>(state >> 24U));
        }
        std::size_t steps = 0U;
        for (std::size_t offset = 0U; offset < noise.size(); ++steps) {
            const std::size_t next = NextUITextGraphemeBoundary(noise, offset);
            Require(next > offset && next <= noise.size(), "A grapheme step on random bytes must advance and stay inside the text");
            Require(PreviousUITextGraphemeBoundary(noise, next) <= offset, "The boundary before a step must not lie after its start");
            offset = next;
        }
        Require(steps == UITextGraphemeCount(noise) && steps <= noise.size(), "The step count of random bytes must equal the grapheme count");
    }
    Require(NextUITextGraphemeBoundary("\xC3", 0U) == 1U && NextUITextGraphemeBoundary("\xF0\x9F", 0U) == 1U &&
            UITextGraphemeCount(std::string_view{}) == 0U && PreviousUITextGraphemeBoundary("abc", 0U) == 0U,
        "Truncated sequences and empty text have fixed answers");
}

} // namespace

void RunUITextLineBreakingTests() {
    TestExplicitLineBreaksSurviveWordWrap();
    TestWordWrapBreaksAtSpacesAndSplitsLongWords();
    TestCjkTextWrapsBetweenIdeographsWithKinsoku();
    TestGraphemeClusters();
}

} // namespace kb::tests
