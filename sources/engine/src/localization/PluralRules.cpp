#include "engine/localization/PluralRules.hpp"

#include <array>
#include <cstddef>
#include <limits>
#include <string>
#include <utility>
#include <vector>

namespace kb::localization {
namespace {

// Plural rules per language, written in the CLDR plural rule syntax (UTS #35, Part 3, "Language Plural
// Rules"): a condition is `or`-separated groups of `and`-separated relations `operand [% modulus] (= | !=)
// value-or-range[, ...]`. A category without a condition never matches; `other` is implied for every
// language. Each line lists the space-separated locale ids sharing the rules, then the zero, one, two, few
// and many conditions.
struct PluralRuleSource {
    std::string_view locales;
    std::string_view zero;
    std::string_view one;
    std::string_view two;
    std::string_view few;
    std::string_view many;
};

constexpr std::string_view kMillionMany = "e = 0 and i != 0 and i % 1000000 = 0 and v = 0 or e != 0..5";

constexpr std::array<PluralRuleSource, 21U> kPluralRuleSources{ {
    { "ja ko zh id ms th vi", "", "", "", "", "" },
    { "en de nl sv fi et", "", "i = 1 and v = 0", "", "", "" },
    { "tr hu el bg nb no", "", "n = 1", "", "", "" },
    { "da", "", "n = 1 or t != 0 and i = 0,1", "", "", "" },
    { "hi", "", "i = 0 or n = 1", "", "", "" },
    { "fr", "", "i = 0,1", "", "", kMillionMany },
    { "es", "", "n = 1", "", "", kMillionMany },
    { "it ca", "", "i = 1 and v = 0", "", "", kMillionMany },
    { "pt", "", "i = 0..1", "", "", kMillionMany },
    { "pt-pt", "", "i = 1 and v = 0", "", "", kMillionMany },
    { "pl", "", "i = 1 and v = 0", "",
      "v = 0 and i % 10 = 2..4 and i % 100 != 12..14",
      "v = 0 and i != 1 and i % 10 = 0..1 or v = 0 and i % 10 = 5..9 or v = 0 and i % 100 = 12..14" },
    { "cs sk", "", "i = 1 and v = 0", "", "i = 2..4 and v = 0", "v != 0" },
    { "ru uk", "", "v = 0 and i % 10 = 1 and i % 100 != 11", "",
      "v = 0 and i % 10 = 2..4 and i % 100 != 12..14",
      "v = 0 and i % 10 = 0 or v = 0 and i % 10 = 5..9 or v = 0 and i % 100 = 11..14" },
    { "hr sr bs", "", "v = 0 and i % 10 = 1 and i % 100 != 11 or f % 10 = 1 and f % 100 != 11", "",
      "v = 0 and i % 10 = 2..4 and i % 100 != 12..14 or f % 10 = 2..4 and f % 100 != 12..14", "" },
    { "sl", "", "v = 0 and i % 100 = 1", "v = 0 and i % 100 = 2", "v = 0 and i % 100 = 3..4 or v != 0", "" },
    { "ar", "n = 0", "n = 1", "n = 2", "n % 100 = 3..10", "n % 100 = 11..99" },
    { "he", "", "i = 1 and v = 0 or i = 0 and v != 0", "i = 2 and v = 0", "", "" },
    { "ro", "", "i = 1 and v = 0", "", "v != 0 or n = 0 or n != 1 and n % 100 = 1..19", "" },
    { "lt", "", "n % 10 = 1 and n % 100 != 11..19", "", "n % 10 = 2..9 and n % 100 != 11..19", "f != 0" },
    { "lv", "n % 10 = 0 or n % 100 = 11..19 or v = 2 and f % 100 = 11..19",
      "n % 10 = 1 and n % 100 != 11 or v = 2 and f % 10 = 1 and f % 100 != 11 or v != 2 and f % 10 = 1", "", "", "" },
    { "ga", "", "n = 1", "n = 2", "n = 3..6", "n = 7..10" },
} };

struct PluralRange {
    std::uint64_t low = 0U;
    std::uint64_t high = 0U;
};

struct PluralRelation {
    char operand = 'n';
    std::uint64_t modulus = 0U;
    bool negated = false;
    std::vector<PluralRange> ranges;
};

// `or` of `and` groups.
using PluralCondition = std::vector<std::vector<PluralRelation>>;

struct CompiledPluralRules {
    std::array<PluralCondition, 5U> conditions;
    std::array<PluralCategory, 6U> categories{};
    std::size_t categoryCount = 0U;
};

class PluralRuleParser {
public:
    explicit PluralRuleParser(std::string_view text) noexcept : text_(text) {}

    [[nodiscard]] bool Parse(PluralCondition& condition) {
        condition.clear();
        do {
            std::vector<PluralRelation> group;
            do {
                PluralRelation relation;
                if (!ParseRelation(relation)) return false;
                group.push_back(std::move(relation));
            } while (Keyword("and"));
            condition.push_back(std::move(group));
        } while (Keyword("or"));
        SkipSpace();
        return position_ == text_.size();
    }

private:
    void SkipSpace() noexcept {
        while (position_ < text_.size() && text_[position_] == ' ') ++position_;
    }

    [[nodiscard]] bool Keyword(std::string_view keyword) noexcept {
        SkipSpace();
        if (text_.substr(position_, keyword.size()) != keyword) return false;
        const std::size_t end = position_ + keyword.size();
        if (end < text_.size() && text_[end] != ' ') return false;
        position_ = end;
        return true;
    }

    [[nodiscard]] bool Symbol(std::string_view symbol) noexcept {
        SkipSpace();
        if (text_.substr(position_, symbol.size()) != symbol) return false;
        position_ += symbol.size();
        return true;
    }

    [[nodiscard]] bool Value(std::uint64_t& value) noexcept {
        SkipSpace();
        const std::size_t start = position_;
        value = 0U;
        while (position_ < text_.size() && text_[position_] >= '0' && text_[position_] <= '9') {
            const std::uint64_t digit = static_cast<std::uint64_t>(text_[position_] - '0');
            if (value > (std::numeric_limits<std::uint64_t>::max() - digit) / 10U) return false;
            value = value * 10U + digit;
            ++position_;
        }
        return position_ != start;
    }

    [[nodiscard]] bool ParseRelation(PluralRelation& relation) {
        SkipSpace();
        if (position_ >= text_.size()) return false;
        relation.operand = text_[position_];
        if (std::string_view{ "nivwftec" }.find(relation.operand) == std::string_view::npos) return false;
        if (relation.operand == 'c') relation.operand = 'e';
        ++position_;
        if (Symbol("%")) {
            if (!Value(relation.modulus) || relation.modulus == 0U) return false;
        }
        if (Symbol("!=")) {
            relation.negated = true;
        } else if (!Symbol("=")) {
            return false;
        }
        do {
            PluralRange range;
            if (!Value(range.low)) return false;
            range.high = range.low;
            if (Symbol("..")) {
                if (!Value(range.high) || range.high < range.low) return false;
            }
            relation.ranges.push_back(range);
        } while (Symbol(","));
        return true;
    }

    std::string_view text_;
    std::size_t position_ = 0U;
};

struct PluralRuleTable {
    std::vector<std::pair<std::string, std::size_t>> locales;
    std::vector<CompiledPluralRules> rules;
    CompiledPluralRules otherOnly;
};

[[nodiscard]] CompiledPluralRules Compile(const PluralRuleSource& source) {
    CompiledPluralRules compiled;
    const std::array<std::string_view, 5U> texts{ source.zero, source.one, source.two, source.few, source.many };
    for (std::size_t index = 0U; index < texts.size(); ++index) {
        if (texts[index].empty()) continue;
        PluralCondition condition;
        // The table above is fixed program data; a rule that does not parse is left out rather than
        // matching every number, and the localization tests check every listed language.
        if (!PluralRuleParser{ texts[index] }.Parse(condition)) continue;
        compiled.conditions[index] = std::move(condition);
        compiled.categories[compiled.categoryCount++] = static_cast<PluralCategory>(index);
    }
    compiled.categories[compiled.categoryCount++] = PluralCategory::Other;
    return compiled;
}

[[nodiscard]] const PluralRuleTable& Table() {
    static const PluralRuleTable table = [] {
        PluralRuleTable built;
        built.otherOnly.categories[0] = PluralCategory::Other;
        built.otherOnly.categoryCount = 1U;
        for (const PluralRuleSource& source : kPluralRuleSources) {
            const std::size_t index = built.rules.size();
            built.rules.push_back(Compile(source));
            std::string_view remaining = source.locales;
            while (!remaining.empty()) {
                const std::size_t space = remaining.find(' ');
                built.locales.emplace_back(std::string{ remaining.substr(0U, space) }, index);
                remaining = space == std::string_view::npos ? std::string_view{} : remaining.substr(space + 1U);
            }
        }
        return built;
    }();
    return table;
}

[[nodiscard]] const CompiledPluralRules* FindRules(std::string_view language) {
    std::string normalized;
    normalized.reserve(language.size());
    for (const char character : language) {
        if (character == '_') {
            normalized.push_back('-');
        } else if (character >= 'A' && character <= 'Z') {
            normalized.push_back(static_cast<char>(character - 'A' + 'a'));
        } else {
            normalized.push_back(character);
        }
    }
    const PluralRuleTable& table = Table();
    while (!normalized.empty()) {
        for (const auto& [locale, index] : table.locales) {
            if (locale == normalized) return &table.rules[index];
        }
        const std::size_t dash = normalized.rfind('-');
        if (dash == std::string::npos) break;
        normalized.resize(dash);
    }
    return nullptr;
}

[[nodiscard]] bool InRanges(const PluralRelation& relation, std::uint64_t value) noexcept {
    for (const PluralRange& range : relation.ranges) {
        if (value >= range.low && value <= range.high) return true;
    }
    return false;
}

[[nodiscard]] bool Holds(const PluralRelation& relation, const PluralOperands& operands) noexcept {
    std::uint64_t value = 0U;
    switch (relation.operand) {
    case 'n':
        // A number with a non-zero fraction equals no integer, so it is in no range.
        if (operands.t != 0U) return relation.negated;
        value = operands.i;
        break;
    case 'i': value = operands.i; break;
    case 'v': value = operands.v; break;
    case 'w': value = operands.w; break;
    case 'f': value = operands.f; break;
    case 't': value = operands.t; break;
    default: value = operands.e; break;
    }
    if (relation.modulus != 0U) value %= relation.modulus;
    return InRanges(relation, value) != relation.negated;
}

[[nodiscard]] bool Holds(const PluralCondition& condition, const PluralOperands& operands) noexcept {
    for (const std::vector<PluralRelation>& group : condition) {
        bool all = true;
        for (const PluralRelation& relation : group) {
            if (!Holds(relation, operands)) {
                all = false;
                break;
            }
        }
        if (all) return true;
    }
    return false;
}

} // namespace

std::string_view PluralCategoryName(PluralCategory category) noexcept {
    switch (category) {
    case PluralCategory::Zero: return "zero";
    case PluralCategory::One: return "one";
    case PluralCategory::Two: return "two";
    case PluralCategory::Few: return "few";
    case PluralCategory::Many: return "many";
    case PluralCategory::Other: return "other";
    }
    return "other";
}

std::optional<PluralCategory> ParsePluralCategoryName(std::string_view name) noexcept {
    for (std::uint8_t index = 0U; index <= static_cast<std::uint8_t>(PluralCategory::Other); ++index) {
        const auto category = static_cast<PluralCategory>(index);
        if (PluralCategoryName(category) == name) return category;
    }
    return std::nullopt;
}

PluralOperands PluralOperandsFromInteger(std::int64_t value) noexcept {
    const std::uint64_t absolute = value < 0
        ? static_cast<std::uint64_t>(-(value + 1)) + 1U
        : static_cast<std::uint64_t>(value);
    return PluralOperands{ .i = absolute };
}

std::optional<PluralOperands> ParsePluralOperands(std::string_view number) noexcept {
    constexpr std::size_t kMaximumDigits = 18U;
    std::size_t position = 0U;
    if (position < number.size() && (number[position] == '-' || number[position] == '+')) ++position;
    std::string_view integerDigits;
    std::string_view fractionDigits;
    const std::size_t integerStart = position;
    while (position < number.size() && number[position] >= '0' && number[position] <= '9') ++position;
    integerDigits = number.substr(integerStart, position - integerStart);
    if (integerDigits.empty()) return std::nullopt;
    if (position < number.size() && number[position] == '.') {
        const std::size_t fractionStart = ++position;
        while (position < number.size() && number[position] >= '0' && number[position] <= '9') ++position;
        fractionDigits = number.substr(fractionStart, position - fractionStart);
        if (fractionDigits.empty()) return std::nullopt;
    }
    std::uint32_t exponent = 0U;
    if (position < number.size() && (number[position] == 'c' || number[position] == 'e')) {
        const std::size_t exponentStart = ++position;
        while (position < number.size() && number[position] >= '0' && number[position] <= '9') {
            exponent = exponent * 10U + static_cast<std::uint32_t>(number[position] - '0');
            if (exponent > kMaximumDigits) return std::nullopt;
            ++position;
        }
        if (position == exponentStart) return std::nullopt;
    }
    if (position != number.size()) return std::nullopt;

    // A compact exponent moves fraction digits into the integer part: "1.25c2" is 125.
    std::string integerText{ integerDigits };
    std::string fractionText{ fractionDigits };
    for (std::uint32_t shift = 0U; shift < exponent; ++shift) {
        if (fractionText.empty()) {
            integerText.push_back('0');
        } else {
            integerText.push_back(fractionText.front());
            fractionText.erase(fractionText.begin());
        }
    }
    const std::size_t firstSignificant = integerText.find_first_not_of('0');
    integerText.erase(0U, firstSignificant == std::string::npos ? integerText.size() - 1U : firstSignificant);
    if (integerText.size() > kMaximumDigits || fractionText.size() > kMaximumDigits) return std::nullopt;

    PluralOperands operands{ .e = exponent };
    for (const char digit : integerText) operands.i = operands.i * 10U + static_cast<std::uint64_t>(digit - '0');
    operands.v = static_cast<std::uint32_t>(fractionText.size());
    for (const char digit : fractionText) operands.f = operands.f * 10U + static_cast<std::uint64_t>(digit - '0');
    const std::size_t lastSignificant = fractionText.find_last_not_of('0');
    const std::string_view trimmed = lastSignificant == std::string::npos
        ? std::string_view{} : std::string_view{ fractionText }.substr(0U, lastSignificant + 1U);
    operands.w = static_cast<std::uint32_t>(trimmed.size());
    for (const char digit : trimmed) operands.t = operands.t * 10U + static_cast<std::uint64_t>(digit - '0');
    return operands;
}

PluralCategory SelectPluralCategory(std::string_view language, const PluralOperands& operands) noexcept {
    const CompiledPluralRules* rules = FindRules(language);
    if (rules == nullptr) return PluralCategory::Other;
    for (std::size_t index = 0U; index < rules->conditions.size(); ++index) {
        if (!rules->conditions[index].empty() && Holds(rules->conditions[index], operands)) {
            return static_cast<PluralCategory>(index);
        }
    }
    return PluralCategory::Other;
}

PluralCategory SelectPluralCategory(std::string_view language, std::int64_t value) noexcept {
    return SelectPluralCategory(language, PluralOperandsFromInteger(value));
}

bool HasPluralRules(std::string_view language) noexcept {
    return FindRules(language) != nullptr;
}

std::span<const PluralCategory> PluralCategoriesOf(std::string_view language) noexcept {
    const CompiledPluralRules* rules = FindRules(language);
    if (rules == nullptr) rules = &Table().otherOnly;
    return std::span<const PluralCategory>{ rules->categories.data(), rules->categoryCount };
}

} // namespace kb::localization
