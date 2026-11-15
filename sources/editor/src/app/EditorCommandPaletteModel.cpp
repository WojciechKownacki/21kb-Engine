#include "app/EditorCommandPaletteModel.hpp"

#include "app/EditorKeyState.hpp"

#include <algorithm>
#include <limits>
#include <utility>

namespace kb::editor {
namespace {

constexpr std::size_t kMaxWords = 4;
constexpr std::size_t kMaxTextLength = 64;

[[nodiscard]] constexpr char Fold(char character) noexcept {
    return character >= 'A' && character <= 'Z' ? static_cast<char>(character + ('a' - 'A')) : character;
}

[[nodiscard]] constexpr bool IsLower(char character) noexcept { return character >= 'a' && character <= 'z'; }
[[nodiscard]] constexpr bool IsUpper(char character) noexcept { return character >= 'A' && character <= 'Z'; }
[[nodiscard]] constexpr bool IsLetterOrDigit(char character) noexcept {
    return IsLower(character) || IsUpper(character) || (character >= '0' && character <= '9') || static_cast<unsigned char>(character) >= 0x80U;
}

// A word starts after a space or a punctuation mark, and where a lower-case letter is followed by a capital.
[[nodiscard]] bool IsWordStart(std::string_view label, std::size_t position) noexcept {
    if (position == 0U) {
        return true;
    }
    const char previous = label[position - 1U];
    return !IsLetterOrDigit(previous) || (IsLower(previous) && IsUpper(label[position]));
}

constexpr int kExact = 100000;
constexpr int kPrefix = 90000;
constexpr int kWordStartRun = 80000;
constexpr int kRun = 60000;
constexpr int kLetters = 40000;

} // namespace

EditorCommandPaletteLayout EditorCommandPaletteLayout::Resolve(int windowWidth, int windowHeight, int visibleRows) noexcept {
    constexpr int kPadding = 8;
    constexpr int kFieldHeight = 34;
    constexpr int kRowHeight = 28;
    constexpr int kHintHeight = 24;
    constexpr int kGap = 6;
    EditorCommandPaletteLayout layout;
    layout.rowCount = std::clamp(visibleRows, 0, kVisibleRows);
    const int listRows = std::max(1, layout.rowCount);
    const int width = std::min(std::max(windowWidth - 24, 1), std::clamp(windowWidth * 3 / 5, 360, 680));
    const int height = kPadding + kFieldHeight + kGap + (listRows * kRowHeight) + kGap + kHintHeight + kPadding;
    const int left = (windowWidth - width) / 2;
    const int top = std::min(std::max(12, windowHeight / 6), std::max(0, windowHeight - height - 12));
    layout.panel = { left, top, left + width, top + height };
    layout.field = { left + kPadding, top + kPadding, left + width - kPadding, top + kPadding + kFieldHeight };
    const int listTop = layout.field.bottom + kGap;
    layout.list = { left + 4, listTop, left + width - 4, listTop + (listRows * kRowHeight) };
    for (int row = 0; row < layout.rowCount; ++row) {
        const int rowTop = listTop + (row * kRowHeight);
        layout.rows[static_cast<std::size_t>(row)] = { layout.list.left, rowTop, layout.list.right, rowTop + kRowHeight };
    }
    layout.hint = { left + kPadding, layout.list.bottom + kGap, left + width - kPadding, layout.list.bottom + kGap + kHintHeight };
    return layout;
}

int EditorCommandPaletteLayout::RowAt(int x, int y) const noexcept {
    for (int row = 0; row < rowCount; ++row) {
        if (rows[static_cast<std::size_t>(row)].Contains(x, y)) {
            return row;
        }
    }
    return -1;
}

int EditorCommandPaletteModel::ScoreWord(std::string_view word, std::string_view label, std::string_view folded) noexcept {
    if (word.empty()) {
        return 0;
    }
    if (word.size() > folded.size()) {
        return -1;
    }
    const int length = static_cast<int>(std::min<std::size_t>(folded.size(), 500U));
    if (folded == word) {
        return kExact;
    }
    if (folded.starts_with(word)) {
        return kPrefix - length;
    }
    std::size_t at = folded.find(word);
    if (at != std::string_view::npos) {
        const int first = static_cast<int>(std::min<std::size_t>(at, 500U));
        while (at != std::string_view::npos) {
            if (IsWordStart(label, at)) {
                return kWordStartRun - static_cast<int>(std::min<std::size_t>(at, 500U)) - length;
            }
            at = folded.find(word, at + 1U);
        }
        return kRun - first - length;
    }
    // The letters in order: a letter that starts a word or follows the one before counts for more.
    int bonus = 0;
    std::size_t position = 0;
    std::size_t previous = std::string_view::npos;
    std::size_t first = std::string_view::npos;
    for (const char letter : word) {
        const std::size_t found = folded.find(letter, position);
        if (found == std::string_view::npos) {
            return -1;
        }
        if (IsWordStart(label, found)) {
            bonus += 300;
        }
        if (previous != std::string_view::npos) {
            bonus += found == previous + 1U ? 150 : -5 * static_cast<int>(std::min<std::size_t>(found - previous - 1U, 20U));
        }
        if (first == std::string_view::npos) {
            first = found;
        }
        previous = found;
        position = found + 1U;
    }
    return kLetters + std::clamp(bonus, -10000, 15000) - static_cast<int>(std::min<std::size_t>(first, 500U)) - length;
}

void EditorCommandPaletteModel::Index::Clear() noexcept {
    std::vector<Candidate>().swap(candidates_);
    std::string().swap(text_);
    std::vector<Match>().swap(matches_);
    std::vector<std::uint32_t>().swap(top_);
    lastText_.clear();
    filtered_ = false;
    matchCount_ = 0;
    scanned_ = 0;
}

void EditorCommandPaletteModel::Index::ClearResults() noexcept {
    matches_.clear();
    top_.clear();
    lastText_.clear();
    filtered_ = false;
    matchCount_ = 0;
    scanned_ = 0;
}

void EditorCommandPaletteModel::Index::Add(
    std::string_view label, std::string_view detail, std::string_view shortcut, std::uint64_t value) {
    constexpr std::size_t kLimit = std::numeric_limits<std::uint32_t>::max() / 2U;
    if (label.empty() || candidates_.size() >= kLimit || text_.size() + (2U * label.size()) + detail.size() + shortcut.size() >= kLimit) {
        return;
    }
    candidates_.push_back({
        .value = value,
        .offset = static_cast<std::uint32_t>(text_.size()),
        .labelLength = static_cast<std::uint32_t>(label.size()),
        .detailLength = static_cast<std::uint32_t>(detail.size()),
        .shortcutLength = static_cast<std::uint32_t>(shortcut.size()),
    });
    text_.append(label);
    for (const char letter : label) {
        text_.push_back(Fold(letter));
    }
    text_.append(detail);
    text_.append(shortcut);
    filtered_ = false;
}

int EditorCommandPaletteModel::Index::Score(const Candidate& candidate, std::span<const std::string_view> words) const noexcept {
    const std::string_view label(text_.data() + candidate.offset, candidate.labelLength);
    const std::string_view folded(text_.data() + candidate.offset + candidate.labelLength, candidate.labelLength);
    int total = 0;
    for (const std::string_view word : words) {
        const int score = ScoreWord(word, label, folded);
        if (score < 0) {
            return -1;
        }
        total += score;
    }
    return total;
}

void EditorCommandPaletteModel::Index::Filter(std::string_view text) {
    std::array<std::string_view, kMaxWords> words{};
    std::size_t wordCount = 0;
    for (std::size_t start = 0; start < text.size() && wordCount < kMaxWords;) {
        std::size_t end = text.find(' ', start);
        if (end == std::string_view::npos) {
            end = text.size();
        }
        if (end > start) {
            words[wordCount++] = text.substr(start, end - start);
        }
        start = end + 1U;
    }
    scanned_ = 0;
    top_.clear();
    if (wordCount == 0U) {
        filtered_ = false;
        lastText_.clear();
        matches_.clear();
        matchCount_ = candidates_.size();
        const std::size_t shown = std::min(kMaxResults, candidates_.size());
        for (std::size_t index = 0; index < shown; ++index) {
            top_.push_back(static_cast<std::uint32_t>(index));
        }
        return;
    }

    // Whatever matches a longer text matches the shorter one it starts with, so the names the last text
    // matched are the only ones left to look at.
    const bool narrowing = filtered_ && text.size() >= lastText_.size() && text.starts_with(lastText_);
    const std::span<const std::string_view> wordSpan(words.data(), wordCount);
    std::vector<Match> next;
    if (narrowing) {
        scanned_ = matches_.size();
        next.reserve(matches_.size());
        for (const Match& match : matches_) {
            if (const int score = Score(candidates_[match.candidate], wordSpan); score >= 0) {
                next.push_back({ match.candidate, score });
            }
        }
    } else {
        scanned_ = candidates_.size();
        for (std::size_t index = 0; index < candidates_.size(); ++index) {
            if (const int score = Score(candidates_[index], wordSpan); score >= 0) {
                next.push_back({ static_cast<std::uint32_t>(index), score });
            }
        }
    }
    matches_ = std::move(next);
    lastText_.assign(text);
    filtered_ = true;
    matchCount_ = matches_.size();

    const auto better = [this](const Match& left, const Match& right) {
        if (left.score != right.score) {
            return left.score > right.score;
        }
        const std::uint32_t leftLength = candidates_[left.candidate].labelLength;
        const std::uint32_t rightLength = candidates_[right.candidate].labelLength;
        return leftLength != rightLength ? leftLength < rightLength : left.candidate < right.candidate;
    };
    std::vector<Match> best(std::min(kMaxResults, matches_.size()));
    std::partial_sort_copy(matches_.begin(), matches_.end(), best.begin(), best.end(), better);
    top_.reserve(best.size());
    for (const Match& match : best) {
        top_.push_back(match.candidate);
    }
}

EditorCommandPaletteModel::Row EditorCommandPaletteModel::Index::ResultAt(std::size_t index) const {
    if (index >= top_.size()) {
        return {};
    }
    const Candidate& candidate = candidates_[top_[index]];
    const char* const base = text_.data() + candidate.offset;
    const std::size_t detailStart = 2U * static_cast<std::size_t>(candidate.labelLength);
    return {
        .label = std::string_view(base, candidate.labelLength),
        .detail = std::string_view(base + detailStart, candidate.detailLength),
        .shortcut = std::string_view(base + detailStart + candidate.detailLength, candidate.shortcutLength),
        .value = candidate.value,
    };
}

EditorCommandPaletteModel::Index& EditorCommandPaletteModel::Active() noexcept {
    return mode_ == Mode::Commands ? commands_ : assets_;
}

const EditorCommandPaletteModel::Index& EditorCommandPaletteModel::Active() const noexcept {
    return mode_ == Mode::Commands ? commands_ : assets_;
}

void EditorCommandPaletteModel::Open(std::string_view initialText) {
    open_ = true;
    field_.Focus(true);
    field_.SetQuery(std::string{ initialText });
    TextChanged();
}

void EditorCommandPaletteModel::Close() noexcept {
    open_ = false;
    field_.Focus(false);
    field_.Clear();
    commands_.Clear();
    assets_.Clear();
    selected_ = -1;
    first_ = 0;
    ++revision_;
}

bool EditorCommandPaletteModel::IsOpen() const noexcept {
    return open_;
}

void EditorCommandPaletteModel::ClearEntries() noexcept {
    commands_.Clear();
    assets_.Clear();
}

void EditorCommandPaletteModel::AddCommand(
    std::string_view label, std::string_view detail, std::string_view shortcut, std::uint64_t value) {
    commands_.Add(label, detail, shortcut, value);
}

void EditorCommandPaletteModel::AddAsset(std::string_view label, std::string_view detail, std::uint64_t value) {
    assets_.Add(label, detail, {}, value);
}

EditorHierarchySearchState& EditorCommandPaletteModel::Field() noexcept {
    return field_;
}

const EditorHierarchySearchState& EditorCommandPaletteModel::Field() const noexcept {
    return field_;
}

void EditorCommandPaletteModel::TextChanged() {
    const std::string_view typed = field_.Query();
    mode_ = !typed.empty() && typed.front() == kCommandPrefix ? Mode::Commands : Mode::Assets;
    const std::string_view body = mode_ == Mode::Commands ? typed.substr(1) : typed;
    // Words are separated by single spaces, in lower case, and the text has a length limit.
    std::string text;
    bool pendingSpace = false;
    for (const char letter : body) {
        if (letter == ' ') {
            pendingSpace = !text.empty();
            continue;
        }
        if (text.size() + (pendingSpace ? 2U : 1U) > kMaxTextLength) {
            break;
        }
        if (pendingSpace) {
            text.push_back(' ');
            pendingSpace = false;
        }
        text.push_back(Fold(letter));
    }
    if (mode_ == Mode::Assets && text.empty()) {
        // Nothing to rank assets by yet: the hint under the box says what to type.
        assets_.ClearResults();
        selected_ = -1;
        first_ = 0;
        ++revision_;
        return;
    }
    Active().Filter(text);
    selected_ = Active().ResultCount() > 0U ? 0 : -1;
    first_ = 0;
    ++revision_;
}

EditorCommandPaletteModel::Mode EditorCommandPaletteModel::CurrentMode() const noexcept {
    return mode_;
}

std::size_t EditorCommandPaletteModel::EntryCount() const noexcept {
    return Active().Count();
}

std::size_t EditorCommandPaletteModel::MatchCount() const noexcept {
    return Active().MatchCount();
}

std::size_t EditorCommandPaletteModel::Scanned() const noexcept {
    return Active().Scanned();
}

std::size_t EditorCommandPaletteModel::ResultCount() const noexcept {
    return Active().ResultCount();
}

EditorCommandPaletteModel::Row EditorCommandPaletteModel::ResultAt(std::size_t index) const {
    return Active().ResultAt(index);
}

int EditorCommandPaletteModel::Selected() const noexcept {
    return selected_;
}

int EditorCommandPaletteModel::FirstVisible() const noexcept {
    return first_;
}

int EditorCommandPaletteModel::VisibleCount() const noexcept {
    return std::max(0, std::min(EditorCommandPaletteLayout::kVisibleRows, static_cast<int>(ResultCount()) - first_));
}

void EditorCommandPaletteModel::EnsureSelectionVisible() noexcept {
    constexpr int kRows = EditorCommandPaletteLayout::kVisibleRows;
    if (selected_ < first_) {
        first_ = std::max(0, selected_);
    } else if (selected_ >= first_ + kRows) {
        first_ = selected_ - kRows + 1;
    }
}

void EditorCommandPaletteModel::MoveSelection(int delta, bool wrap) noexcept {
    const int count = static_cast<int>(ResultCount());
    if (count == 0) {
        return;
    }
    int next = selected_ + delta;
    if (wrap) {
        next %= count;
        if (next < 0) {
            next += count;
        }
    } else {
        next = std::clamp(next, 0, count - 1);
    }
    if (next != selected_) {
        selected_ = next;
        EnsureSelectionVisible();
        ++revision_;
    }
}

EditorCommandPaletteModel::KeyResult EditorCommandPaletteModel::HandleKey(Key key) noexcept {
    switch (key) {
    case Key::Escape:
        return KeyResult::Close;
    case Key::Enter:
        return KeyResult::Choose;
    case Key::Up:
        MoveSelection(-1, true);
        break;
    case Key::Down:
        MoveSelection(1, true);
        break;
    case Key::PageUp:
        MoveSelection(-EditorCommandPaletteLayout::kVisibleRows, false);
        break;
    case Key::PageDown:
        MoveSelection(EditorCommandPaletteLayout::kVisibleRows, false);
        break;
    }
    return KeyResult::Moved;
}

bool EditorCommandPaletteModel::IsOpenShortcut(int virtualKey) noexcept {
    constexpr int kShift = 0x10;
    constexpr int kControl = 0x11;
    constexpr int kAlt = 0x12;
    return virtualKey == 'P' && EditorKeyDown(kControl) && EditorKeyDown(kShift) && !EditorKeyDown(kAlt);
}

bool EditorCommandPaletteModel::SelectVisible(int slot) noexcept {
    if (slot < 0 || slot >= VisibleCount() || first_ + slot == selected_) {
        return false;
    }
    selected_ = first_ + slot;
    ++revision_;
    return true;
}

std::optional<EditorCommandPaletteModel::Row> EditorCommandPaletteModel::Chosen() const {
    if (selected_ < 0 || static_cast<std::size_t>(selected_) >= ResultCount()) {
        return std::nullopt;
    }
    return ResultAt(static_cast<std::size_t>(selected_));
}

std::uint64_t EditorCommandPaletteModel::Revision() const noexcept {
    return revision_;
}

} // namespace kb::editor
