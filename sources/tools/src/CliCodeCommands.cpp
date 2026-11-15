// kb_cli code: a map of the engine's source for a reader that cannot read all of it. Everything is read from
// the source tree when the command runs, so the answers are never older than the code. modules (CliCodeModules.cpp)
// is the machine-readable map; map, find and show are the text views down to files and declarations.
#include "CliCommands.hpp"

#include "engine/platform/FileSystemPath.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace kb::cli {
namespace {

namespace fs = std::filesystem;

constexpr std::size_t kSummaryLength = 150U;

struct SourceFile {
    std::string path; // relative to the engine root, forward slashes
    std::vector<std::string> lines;
    std::string summary;
};

[[nodiscard]] std::string Lower(std::string_view text) {
    std::string lowered{ text };
    std::ranges::transform(lowered, lowered.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return lowered;
}

[[nodiscard]] std::string_view Trimmed(std::string_view text) noexcept {
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front())) != 0) text.remove_prefix(1U);
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back())) != 0) text.remove_suffix(1U);
    return text;
}

[[nodiscard]] bool IsSource(const fs::path& path) {
    const std::string extension = Lower(path.extension().string());
    return extension == ".hpp" || extension == ".h" || extension == ".cpp" || extension == ".c" || extension == ".inl" ||
        extension == ".py" || extension == ".sc" || extension == ".sh" || extension == ".cmake" ||
        path.filename() == "CMakeLists.txt";
}

[[nodiscard]] bool IsIdentifierCharacter(char c) noexcept {
    return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_';
}

// The text of a comment line without its marker, or nothing when the line is not a comment.
[[nodiscard]] std::optional<std::string_view> CommentText(std::string_view line) noexcept {
    line = Trimmed(line);
    for (const std::string_view marker : { "///", "//", "#", "*", "/*", "\"\"\"" }) {
        if (line.starts_with(marker) && !line.starts_with("#include") && !line.starts_with("#pragma") &&
            !line.starts_with("#if") && !line.starts_with("#define") && !line.starts_with("#endif") && !line.starts_with("#!")) {
            return Trimmed(line.substr(marker.size()));
        }
    }
    return std::nullopt;
}

// The comment block that ends on the line before `line`, as one paragraph.
[[nodiscard]] std::string CommentAbove(const std::vector<std::string>& lines, std::size_t line) {
    std::size_t first = line;
    while (first > 0U && CommentText(lines[first - 1U]).has_value()) --first;
    std::string text;
    for (std::size_t index = first; index < line; ++index) {
        const std::string_view part = *CommentText(lines[index]);
        if (part.empty()) continue;
        if (!text.empty()) text += ' ';
        text += part;
    }
    return text;
}

[[nodiscard]] std::string Shortened(std::string text) {
    if (text.size() > kSummaryLength) {
        text.resize(kSummaryLength - 3U);
        text += "...";
    }
    return text;
}

// What a file is for: the comment it opens with, or the one above its first type.
[[nodiscard]] std::string Summary(const std::vector<std::string>& lines) {
    std::size_t line = 0U;
    while (line < lines.size() && line < 60U && !CommentText(lines[line]).has_value()) {
        const std::string_view text = Trimmed(lines[line]);
        if (!text.empty() && !text.starts_with('#') && !text.starts_with("namespace") && !text.starts_with("extern") &&
            !text.starts_with("import") && !text.starts_with("from")) {
            break;
        }
        ++line;
    }
    std::size_t end = line;
    while (end < lines.size() && CommentText(lines[end]).has_value()) ++end;
    if (end > line) return Shortened(CommentAbove(lines, end));
    for (std::size_t index = 0U; index < lines.size(); ++index) {
        const std::string_view text = Trimmed(lines[index]);
        if ((text.starts_with("class ") || text.starts_with("struct ")) && index > 0U && CommentText(lines[index - 1U]).has_value()) {
            return Shortened(CommentAbove(lines, index));
        }
    }
    return {};
}

[[nodiscard]] fs::path EngineRoot(const ArgumentList& arguments) {
    const auto holdsEngine = [](const fs::path& root) {
        std::error_code error;
        return fs::is_directory(root / "sources" / "engine", error) && fs::is_regular_file(root / "CMakeLists.txt", error);
    };
    if (const std::optional<std::string> given = arguments.Option("--engine-root"); given.has_value()) {
        return holdsEngine(*given) ? fs::path{ *given } : fs::path{};
    }
    std::error_code error;
    for (const fs::path& start : { kb::platform::RunningExecutablePath().parent_path(), fs::current_path(error) }) {
        for (fs::path root = start; !root.empty(); root = root.parent_path()) {
            if (holdsEngine(root)) return root;
            if (root == root.parent_path()) break;
        }
    }
    return {};
}

[[nodiscard]] std::vector<SourceFile> ReadSources(const fs::path& root, const fs::path& below) {
    std::vector<SourceFile> files;
    std::error_code error;
    for (fs::recursive_directory_iterator entry{ root / below, fs::directory_options::skip_permission_denied, error }, end;
         !error && entry != end; entry.increment(error)) {
        const std::string name = entry->path().filename().string();
        if (entry->is_directory(error)) {
            if (name.starts_with('.') || name == "Saved" || name == "build" || name == "third_party" || name == "__pycache__") {
                entry.disable_recursion_pending();
            }
            continue;
        }
        if (!entry->is_regular_file(error) || !IsSource(entry->path())) continue;
        std::ifstream stream{ entry->path(), std::ios::binary };
        SourceFile file{ .path = fs::relative(entry->path(), root, error).generic_string() };
        for (std::string line; std::getline(stream, line);) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            file.lines.push_back(std::move(line));
        }
        file.summary = Summary(file.lines);
        files.push_back(std::move(file));
    }
    std::ranges::sort(files, {}, &SourceFile::path);
    return files;
}

// The name a line declares (class, struct, enum, alias or a function with a body or a prototype), or nothing.
[[nodiscard]] std::string DeclaredName(std::string_view line) {
    std::string_view text = Trimmed(line);
    if (text.empty() || text.starts_with("//") || text.starts_with('#') || text.starts_with("return") || text.starts_with('}')) return {};
    for (const std::string_view keyword : { "class ", "struct ", "enum class ", "enum ", "using ", "def " }) {
        const std::size_t at = text.find(keyword);
        if (at != 0U && !(at != std::string_view::npos && text.substr(0U, at).find("template") != std::string_view::npos)) continue;
        std::string_view rest = Trimmed(text.substr(at + keyword.size()));
        std::size_t length = 0U;
        while (length < rest.size() && IsIdentifierCharacter(rest[length])) ++length;
        if (length == 0U || Trimmed(rest.substr(length)).starts_with(';')) return {};
        return std::string{ rest.substr(0U, length) };
    }
    // A function: the identifier in front of the first '(' of a line that ends a declaration.
    const std::size_t paren = text.find('(');
    if (paren == std::string_view::npos || paren == 0U || !(text.ends_with(';') || text.ends_with('{') || text.ends_with(',') || text.ends_with(")"))) return {};
    if (text.find('=') < paren || text.starts_with("if") || text.starts_with("for") || text.starts_with("while") || text.starts_with("switch")) return {};
    std::size_t end = paren;
    while (end > 0U && text[end - 1U] == ' ') --end;
    std::size_t begin = end;
    while (begin > 0U && IsIdentifierCharacter(text[begin - 1U])) --begin;
    if (begin == end || begin == 0U) return {};
    return std::string{ text.substr(begin, end - begin) };
}

[[nodiscard]] std::string Directory(std::string_view path) {
    const std::size_t slash = path.rfind('/');
    return slash == std::string_view::npos ? std::string{} : std::string{ path.substr(0U, slash) };
}

[[nodiscard]] std::size_t Depth(std::string_view path) noexcept {
    return static_cast<std::size_t>(std::ranges::count(path, '/'));
}

int RunMap(const fs::path& root, const ArgumentList& arguments, CommandIo io) {
    const std::string below = fs::path{ arguments.Option("--path").value_or("sources") }.generic_string();
    const std::size_t depth = static_cast<std::size_t>(std::clamp(std::atoi(arguments.Option("--depth").value_or("2").c_str()), 1, 8));
    const std::vector<SourceFile> files = ReadSources(root, below);
    if (files.empty()) {
        io.err << "error: there are no source files under " << below << "\n";
        return 1;
    }
    struct Folder {
        std::size_t files = 0U;
        std::size_t lines = 0U;
        std::string summary;
        std::vector<std::pair<std::size_t, std::string>> largest; // of the files directly in it
    };
    std::map<std::string, Folder> folders;
    const std::size_t baseDepth = Depth(below);
    for (const SourceFile& file : files) {
        for (std::string folder = Directory(file.path); folder.size() >= below.size(); folder = Directory(folder)) {
            Folder& entry = folders[folder];
            ++entry.files;
            entry.lines += file.lines.size();
            if (folder.size() == below.size()) break;
        }
        // A folder is described by its build file; one without such a description is known by its largest files.
        if (file.path.ends_with("/CMakeLists.txt")) {
            if (!file.summary.empty()) folders[Directory(file.path)].summary = file.summary;
        } else {
            folders[Directory(file.path)].largest.emplace_back(file.lines.size(), file.path.substr(file.path.rfind('/') + 1U));
        }
    }
    for (auto& [folder, entry] : folders) {
        if (!entry.summary.empty() || entry.largest.empty()) continue;
        std::ranges::sort(entry.largest, std::greater{});
        entry.summary = "largest files:";
        for (std::size_t index = 0U; index < entry.largest.size() && index < 4U; ++index) {
            entry.summary += (index == 0U ? " " : ", ") + entry.largest[index].second;
        }
    }
    for (const auto& [folder, entry] : folders) {
        if (Depth(folder) > baseDepth + depth) continue;
        io.out << std::string(2U * (Depth(folder) - baseDepth), ' ') << folder << "/  (" << entry.files << " files, " << entry.lines << " lines)";
        if (!entry.summary.empty()) io.out << " - " << entry.summary;
        io.out << '\n';
    }
    // The files of the folder itself, each with what it is for.
    for (const SourceFile& file : files) {
        if (Directory(file.path) != below) continue;
        io.out << "  " << file.path << "  (" << file.lines.size() << " lines)";
        if (!file.summary.empty()) io.out << " - " << file.summary;
        io.out << '\n';
    }
    io.out << "Next: kb_cli code modules --topic <words>, kb_cli code map --path <folder>, kb_cli code find --text <words>, kb_cli code show --symbol <Name>\n";
    return 0;
}

int RunFind(const fs::path& root, const ArgumentList& arguments, CommandIo io) {
    std::vector<std::string> words;
    std::istringstream text{ Lower(arguments.Option("--text").value_or(std::string{})) };
    for (std::string word; text >> word;) words.push_back(word);
    if (words.empty()) {
        io.err << "error: code find requires --text <words>\n";
        return 1;
    }
    const std::size_t limit = static_cast<std::size_t>(std::clamp(std::atoi(arguments.Option("--limit").value_or("15").c_str()), 1, 200));
    struct Hit { std::size_t score = 0U; const SourceFile* file = nullptr; std::size_t line = 0U; std::size_t lineScore = 0U; };
    const std::vector<SourceFile> files = ReadSources(root, arguments.Option("--path").value_or("sources"));
    std::vector<Hit> hits;
    for (const SourceFile& file : files) {
        Hit hit{ .file = &file };
        const std::string path = Lower(file.path);
        const std::string summary = Lower(file.summary);
        std::size_t matchedWords = 0U;
        for (const std::string& word : words) {
            std::size_t wordScore = 0U;
            if (path.find(word) != std::string::npos) wordScore += 12U;
            if (summary.find(word) != std::string::npos) wordScore += 6U;
            std::size_t mentions = 0U;
            for (std::size_t index = 0U; index < file.lines.size(); ++index) {
                const std::string line = Lower(file.lines[index]);
                if (line.find(word) == std::string::npos) continue;
                ++mentions;
                // A line that declares something named after the word is where to start reading.
                const std::size_t lineScore = Lower(DeclaredName(file.lines[index])).find(word) != std::string::npos ? 8U : 1U;
                if (lineScore > hit.lineScore) {
                    hit.lineScore = lineScore;
                    hit.line = index + 1U;
                }
                if (lineScore > 1U) wordScore += 4U;
            }
            wordScore += std::min<std::size_t>(mentions, 6U);
            if (wordScore != 0U) ++matchedWords;
            hit.score += wordScore;
        }
        // Every word has to be there; a public header says more than the source behind it.
        if (matchedWords != words.size()) continue;
        if (file.path.find("/include/") != std::string::npos) hit.score += 4U;
        if (file.path.find("/tests/") != std::string::npos) hit.score = hit.score / 2U;
        hits.push_back(hit);
    }
    std::ranges::stable_sort(hits, [](const Hit& left, const Hit& right) { return left.score > right.score; });
    if (hits.empty()) {
        io.err << "error: nothing in the source mentions all of: " << arguments.Option("--text").value_or(std::string{}) << "\n";
        return 1;
    }
    for (std::size_t index = 0U; index < hits.size() && index < limit; ++index) {
        const Hit& hit = hits[index];
        io.out << hit.file->path << ':' << std::max<std::size_t>(hit.line, 1U);
        if (!hit.file->summary.empty()) io.out << " - " << hit.file->summary;
        io.out << '\n';
        if (hit.line != 0U) io.out << "    " << Trimmed(hit.file->lines[hit.line - 1U]).substr(0U, kSummaryLength) << '\n';
    }
    if (hits.size() > limit) io.out << "(" << hits.size() - limit << " more; narrow the words or pass --limit)\n";
    return 0;
}

int RunShow(const fs::path& root, const ArgumentList& arguments, CommandIo io) {
    const std::string symbol = arguments.Option("--symbol").value_or(std::string{});
    if (symbol.empty()) {
        io.err << "error: code show requires --symbol <Name>\n";
        return 1;
    }
    const std::vector<SourceFile> files = ReadSources(root, "sources");
    const auto mentions = [&symbol](std::string_view line) {
        for (std::size_t at = line.find(symbol); at != std::string_view::npos; at = line.find(symbol, at + 1U)) {
            const bool startsWord = at == 0U || !IsIdentifierCharacter(line[at - 1U]);
            const bool endsWord = at + symbol.size() >= line.size() || !IsIdentifierCharacter(line[at + symbol.size()]);
            if (startsWord && endsWord) return true;
        }
        return false;
    };
    std::size_t shown = 0U;
    std::vector<std::pair<std::size_t, const SourceFile*>> users;
    for (const SourceFile& file : files) {
        std::size_t uses = 0U;
        const bool header = file.path.ends_with(".hpp") || file.path.ends_with(".h");
        bool shownHere = false; // a type's constructors repeat its name; the type itself is enough
        for (std::size_t index = 0U; index < file.lines.size(); ++index) {
            if (!mentions(file.lines[index])) continue;
            ++uses;
            if (!header || shownHere || shown >= 4U || DeclaredName(file.lines[index]) != symbol) continue;
            ++shown;
            shownHere = true;
            io.out << file.path << ':' << index + 1U << '\n';
            if (const std::string comment = CommentAbove(file.lines, index); !comment.empty()) io.out << "  " << comment << '\n';
            // The declaration: to the end of the statement, or the public face of a type.
            int braces = 0;
            for (std::size_t line = index; line < file.lines.size() && line < index + 60U; ++line) {
                io.out << "    " << file.lines[line] << '\n';
                braces += static_cast<int>(std::ranges::count(file.lines[line], '{')) - static_cast<int>(std::ranges::count(file.lines[line], '}'));
                const std::string_view text = Trimmed(file.lines[line]);
                if (braces <= 0 && (text.ends_with(';') || text.ends_with('}'))) break;
                if (text.starts_with("private:")) break;
            }
        }
        if (uses != 0U) users.emplace_back(uses, &file);
    }
    if (users.empty()) {
        io.err << "error: nothing in the source is called '" << symbol << "'; kb_cli code find --text " << symbol << " looks for it as a word\n";
        return 1;
    }
    std::ranges::stable_sort(users, [](const auto& left, const auto& right) { return left.first > right.first; });
    io.out << "Used in " << users.size() << " files:\n";
    std::size_t listed = 0U;
    for (const auto& [uses, file] : users) {
        if (file->path.find("/tests/") != std::string::npos) continue;
        if (++listed > 10U) break;
        io.out << "  " << file->path << " (" << uses << ")\n";
    }
    listed = 0U;
    for (const auto& [uses, file] : users) {
        if (file->path.find("/tests/") == std::string::npos) continue;
        if (listed++ == 0U) io.out << "Tests:\n";
        if (listed > 6U) break;
        io.out << "  " << file->path << " (" << uses << ")\n";
    }
    return 0;
}

} // namespace

int RunCodeCommand(const ArgumentList& arguments, CommandIo io) {
    const std::string action = arguments.Positionals().empty() ? std::string{} : arguments.Positionals().front();
    if (action != "map" && action != "find" && action != "show" && action != "modules") {
        io.err << "error: code takes modules, map, find or show (kb_cli help code)\n";
        return 1;
    }
    const fs::path root = EngineRoot(arguments);
    if (root.empty()) {
        io.err << "error: the engine source was not found; pass --engine-root <folder with CMakeLists.txt and sources/>\n";
        return 1;
    }
    if (action == "modules") return RunCodeModules(root, arguments, io);
    return action == "map" ? RunMap(root, arguments, io) : action == "find" ? RunFind(root, arguments, io) : RunShow(root, arguments, io);
}

} // namespace kb::cli
