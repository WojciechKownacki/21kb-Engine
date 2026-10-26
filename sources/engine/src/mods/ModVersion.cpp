#include "engine/mods/ModVersion.hpp"

#include <algorithm>
#include <charconv>

namespace kb::mods {
namespace {

[[nodiscard]] bool ParseNumber(std::string_view text, std::uint32_t& out) noexcept {
    if (text.empty() || text.size() > 6U || (text.size() > 1U && text.front() == '0')) {
        return false;
    }
    std::uint32_t value = 0U;
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (error != std::errc{} || end != text.data() + text.size() || value > kMaxModVersionNumber) {
        return false;
    }
    out = value;
    return true;
}

} // namespace

bool ParseModVersion(std::string_view text, ModVersion& out) noexcept {
    const std::size_t firstDot = text.find('.');
    if (firstDot == std::string_view::npos) {
        return false;
    }
    const std::size_t secondDot = text.find('.', firstDot + 1U);
    if (secondDot == std::string_view::npos) {
        return false;
    }
    ModVersion version{};
    if (!ParseNumber(text.substr(0U, firstDot), version.major) ||
        !ParseNumber(text.substr(firstDot + 1U, secondDot - firstDot - 1U), version.minor) ||
        !ParseNumber(text.substr(secondDot + 1U), version.patch)) {
        return false;
    }
    out = version;
    return true;
}

std::string ToString(const ModVersion& version) {
    return std::to_string(version.major) + '.' + std::to_string(version.minor) + '.' + std::to_string(version.patch);
}

bool ParseModVersionRange(std::string_view text, ModVersionRange& out) {
    ModVersionRange range{};
    std::size_t position = 0U;
    const auto add = [&range](ModVersionComparator::Op op, const ModVersion& version) {
        range.comparators.push_back(ModVersionComparator{ op, version });
    };
    bool any = false;
    while (position < text.size()) {
        while (position < text.size() && text[position] == ' ') {
            ++position;
        }
        if (position >= text.size()) {
            break;
        }
        const std::size_t end = std::min(text.find(' ', position), text.size());
        std::string_view token = text.substr(position, end - position);
        position = end;
        if (token == "*") {
            any = true;
            continue;
        }
        ModVersionComparator::Op op = ModVersionComparator::Op::Equal;
        bool caret = false;
        bool tilde = false;
        if (token.starts_with(">=")) {
            op = ModVersionComparator::Op::GreaterEqual;
            token.remove_prefix(2U);
        } else if (token.starts_with("<=")) {
            op = ModVersionComparator::Op::LessEqual;
            token.remove_prefix(2U);
        } else if (token.starts_with(">")) {
            op = ModVersionComparator::Op::Greater;
            token.remove_prefix(1U);
        } else if (token.starts_with("<")) {
            op = ModVersionComparator::Op::Less;
            token.remove_prefix(1U);
        } else if (token.starts_with("=")) {
            token.remove_prefix(1U);
        } else if (token.starts_with("^")) {
            caret = true;
            token.remove_prefix(1U);
        } else if (token.starts_with("~")) {
            tilde = true;
            token.remove_prefix(1U);
        }
        ModVersion version{};
        if (!ParseModVersion(token, version)) {
            return false;
        }
        if (caret) {
            add(ModVersionComparator::Op::GreaterEqual, version);
            add(ModVersionComparator::Op::Less, ModVersion{ version.major + 1U, 0U, 0U });
        } else if (tilde) {
            add(ModVersionComparator::Op::GreaterEqual, version);
            add(ModVersionComparator::Op::Less, ModVersion{ version.major, version.minor + 1U, 0U });
        } else {
            add(op, version);
        }
        if (range.comparators.size() > kMaxModVersionComparators) {
            return false;
        }
    }
    if (any && !range.comparators.empty()) {
        return false;
    }
    if (!any && range.comparators.empty()) {
        return false;
    }
    out = std::move(range);
    return true;
}

bool Satisfies(const ModVersionRange& range, const ModVersion& version) noexcept {
    for (const ModVersionComparator& comparator : range.comparators) {
        bool holds = false;
        switch (comparator.op) {
        case ModVersionComparator::Op::Equal: holds = version == comparator.version; break;
        case ModVersionComparator::Op::Greater: holds = version > comparator.version; break;
        case ModVersionComparator::Op::GreaterEqual: holds = version >= comparator.version; break;
        case ModVersionComparator::Op::Less: holds = version < comparator.version; break;
        case ModVersionComparator::Op::LessEqual: holds = version <= comparator.version; break;
        }
        if (!holds) {
            return false;
        }
    }
    return true;
}

std::string ToString(const ModVersionRange& range) {
    if (range.comparators.empty()) {
        return "*";
    }
    std::string text;
    for (const ModVersionComparator& comparator : range.comparators) {
        if (!text.empty()) {
            text += ' ';
        }
        switch (comparator.op) {
        case ModVersionComparator::Op::Equal: break;
        case ModVersionComparator::Op::Greater: text += '>'; break;
        case ModVersionComparator::Op::GreaterEqual: text += ">="; break;
        case ModVersionComparator::Op::Less: text += '<'; break;
        case ModVersionComparator::Op::LessEqual: text += "<="; break;
        }
        text += ToString(comparator.version);
    }
    return text;
}

} // namespace kb::mods
