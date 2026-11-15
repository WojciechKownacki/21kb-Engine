#pragma once

#include "scene/EditorHierarchySearchState.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace kb::editor {

struct EditorPaletteRect {
    int left = 0;
    int top = 0;
    int right = 0;
    int bottom = 0;

    [[nodiscard]] constexpr bool Contains(int x, int y) const noexcept {
        return x >= left && x < right && y >= top && y < bottom;
    }
};

// Where the palette and its rows sit in a window. Only the rows that fit on screen are laid out, whatever the
// size of the list behind them.
struct EditorCommandPaletteLayout {
    static constexpr int kVisibleRows = 9;

    EditorPaletteRect panel;
    EditorPaletteRect field;
    // Where the rows go, or the "nothing found" line when there are none.
    EditorPaletteRect list;
    EditorPaletteRect hint;
    std::array<EditorPaletteRect, kVisibleRows> rows{};
    // Rows laid out: never more than kVisibleRows.
    int rowCount = 0;

    // `visibleRows` is how many rows of the list are shown now; the panel is centred in the window.
    [[nodiscard]] static EditorCommandPaletteLayout Resolve(int windowWidth, int windowHeight, int visibleRows) noexcept;
    // The laid-out row under a point; -1 for none.
    [[nodiscard]] int RowAt(int x, int y) const noexcept;
};

// The command palette and quick open behind one text box. The box starts with ">" to list the editor's commands;
// without it the list is the project's assets and scenes. Every word typed must match a name, in any order, as
// part of a word, as a run of letters or as letters in order, whatever their case; names that start with a word,
// or hold it whole, rank above the rest. The list keeps the best kMaxResults names, and a longer text only looks
// at the names the shorter one matched, so a keystroke stays cheap with a hundred thousand assets.
class EditorCommandPaletteModel {
public:
    static constexpr std::size_t kMaxResults = 100;
    static constexpr char kCommandPrefix = '>';

    enum class Mode : std::uint8_t { Assets, Commands };

    // A row as it is drawn. `value` is what the one who added the entry gave it.
    struct Row {
        std::string_view label;
        std::string_view detail;
        std::string_view shortcut;
        std::uint64_t value = 0;
    };

    void Open(std::string_view initialText);
    // Closes the palette and lets go of its names.
    void Close() noexcept;
    [[nodiscard]] bool IsOpen() const noexcept;

    // The names of each mode, filled in before the palette opens.
    void ClearEntries() noexcept;
    void AddCommand(std::string_view label, std::string_view detail, std::string_view shortcut, std::uint64_t value);
    void AddAsset(std::string_view label, std::string_view detail, std::uint64_t value);

    [[nodiscard]] EditorHierarchySearchState& Field() noexcept;
    [[nodiscard]] const EditorHierarchySearchState& Field() const noexcept;
    // Filters the list again after the text changed.
    void TextChanged();
    [[nodiscard]] Mode CurrentMode() const noexcept;

    // Names of the current mode, names that match, names the last filtering looked at, and rows kept.
    [[nodiscard]] std::size_t EntryCount() const noexcept;
    [[nodiscard]] std::size_t MatchCount() const noexcept;
    [[nodiscard]] std::size_t Scanned() const noexcept;
    [[nodiscard]] std::size_t ResultCount() const noexcept;
    [[nodiscard]] Row ResultAt(std::size_t index) const;

    // The chosen row (-1 when the list is empty) and the first row of the window of rows on screen.
    [[nodiscard]] int Selected() const noexcept;
    [[nodiscard]] int FirstVisible() const noexcept;
    [[nodiscard]] int VisibleCount() const noexcept;
    void MoveSelection(int delta, bool wrap) noexcept;
    // The keys an open palette answers, and what each asks of whoever owns the palette.
    enum class Key : std::uint8_t { Up, Down, PageUp, PageDown, Escape, Enter };
    enum class KeyResult : std::uint8_t { Moved, Close, Choose };
    [[nodiscard]] KeyResult HandleKey(Key key) noexcept;
    // The key that opens the palette: Ctrl+Shift+P, read through the editor's key state.
    [[nodiscard]] static bool IsOpenShortcut(int virtualKey) noexcept;
    // Chooses the row at a place of the window of rows on screen; false when there is none.
    bool SelectVisible(int slot) noexcept;
    [[nodiscard]] std::optional<Row> Chosen() const;
    // Changes whenever what the palette shows changes.
    [[nodiscard]] std::uint64_t Revision() const noexcept;

    // How well one word matches a name; negative for no match. `label` is the name, `folded` its lower-case form.
    [[nodiscard]] static int ScoreWord(std::string_view word, std::string_view label, std::string_view folded) noexcept;

private:
    class Index {
    public:
        void Clear() noexcept;
        // Keeps the names and forgets what matched.
        void ClearResults() noexcept;
        void Add(std::string_view label, std::string_view detail, std::string_view shortcut, std::uint64_t value);
        // `text` is lower case with single spaces between words.
        void Filter(std::string_view text);
        [[nodiscard]] std::size_t Count() const noexcept { return candidates_.size(); }
        [[nodiscard]] std::size_t MatchCount() const noexcept { return matchCount_; }
        [[nodiscard]] std::size_t Scanned() const noexcept { return scanned_; }
        [[nodiscard]] std::size_t ResultCount() const noexcept { return top_.size(); }
        [[nodiscard]] Row ResultAt(std::size_t index) const;

    private:
        struct Candidate {
            std::uint64_t value = 0;
            std::uint32_t offset = 0;
            std::uint32_t labelLength = 0;
            std::uint32_t detailLength = 0;
            std::uint32_t shortcutLength = 0;
        };
        struct Match {
            std::uint32_t candidate = 0;
            int score = 0;
        };

        [[nodiscard]] int Score(const Candidate& candidate, std::span<const std::string_view> words) const noexcept;

        std::vector<Candidate> candidates_;
        // Per candidate: the name, its lower-case form, the detail and the shortcut, one after another.
        std::string text_;
        std::vector<Match> matches_;
        std::vector<std::uint32_t> top_;
        std::string lastText_;
        bool filtered_ = false;
        std::size_t matchCount_ = 0;
        std::size_t scanned_ = 0;
    };

    [[nodiscard]] Index& Active() noexcept;
    [[nodiscard]] const Index& Active() const noexcept;
    void EnsureSelectionVisible() noexcept;

    EditorHierarchySearchState field_;
    Index commands_;
    Index assets_;
    Mode mode_ = Mode::Commands;
    bool open_ = false;
    int selected_ = -1;
    int first_ = 0;
    std::uint64_t revision_ = 1;
};

} // namespace kb::editor
