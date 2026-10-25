#include "CliProgram.hpp"

#include "engine/core/JsonValue.hpp"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace kb::cli {
namespace {

namespace fs = std::filesystem;
using kb::core::JsonValue;

// A transaction keeps a copy of the project's files; a project bigger than this is refused.
constexpr std::uintmax_t kSnapshotLimitBytes = 512ULL * 1024ULL * 1024ULL;
constexpr std::uintmax_t kDiffReadLimitBytes = 16ULL * 1024ULL * 1024ULL;
constexpr std::size_t kDiffLineLimit = 40U;
constexpr std::size_t kDiffContextLines = 2U;

// Folders of generated or foreign data: neither copied nor restored.
bool IsExcluded(const fs::path& relative) {
    if (relative.empty()) return false;
    const std::string first = relative.begin()->generic_string();
    return first == "Binaries" || first == "Intermediate" || first == "Saved" || first == "Library" ||
           first == "Build" || first == ".git" || first == ".vs";
}

struct Listing {
    std::map<fs::path, std::uintmax_t> files;
    std::set<fs::path> directories;
};

// Every file and folder of the project below the root, by path relative to it.
bool ScanProject(const fs::path& root, Listing& listing, std::string& error) {
    std::error_code ec;
    fs::recursive_directory_iterator it{ root, fs::directory_options::skip_permission_denied, ec };
    for (; !ec && it != fs::recursive_directory_iterator{}; it.increment(ec)) {
        const fs::path relative = it->path().lexically_relative(root);
        if (IsExcluded(relative)) {
            it.disable_recursion_pending();
            continue;
        }
        std::error_code statusError;
        if (it->is_symlink(statusError)) continue;
        if (it->is_directory(statusError)) {
            listing.directories.insert(relative);
        } else if (it->is_regular_file(statusError)) {
            const std::uintmax_t size = it->file_size(statusError);
            if (!statusError) listing.files[relative] = size;
        }
    }
    if (ec) {
        error = "cannot read the project folder: " + ec.message();
        return false;
    }
    return true;
}

bool ReadFile(const fs::path& path, std::string& content) {
    std::ifstream input{ path, std::ios::binary };
    if (!input) return false;
    content.assign(std::istreambuf_iterator<char>{ input }, std::istreambuf_iterator<char>{});
    return !input.bad();
}

// The files as they were before the operations, copied beside the project.
struct Snapshot {
    fs::path root;
    fs::path backup;
    Listing listing;
};

bool TakeSnapshot(Snapshot& snapshot, std::string& error) {
    if (!ScanProject(snapshot.root, snapshot.listing, error)) return false;
    std::uintmax_t total = 0U;
    for (const auto& [path, size] : snapshot.listing.files) {
        total += size;
        if (total > kSnapshotLimitBytes) {
            error = "the project is too large for a transaction (limit " + std::to_string(kSnapshotLimitBytes / (1024U * 1024U)) + " MiB)";
            return false;
        }
    }
    std::error_code ec;
    fs::create_directories(snapshot.backup, ec);
    for (const auto& [path, size] : snapshot.listing.files) {
        fs::create_directories((snapshot.backup / path).parent_path(), ec);
        fs::copy_file(snapshot.root / path, snapshot.backup / path, fs::copy_options::overwrite_existing, ec);
        if (ec) {
            error = "cannot copy " + path.generic_string() + ": " + ec.message();
            return false;
        }
    }
    return true;
}

enum class ChangeKind { Added, Modified, Deleted };

struct Change {
    fs::path path;
    ChangeKind kind = ChangeKind::Modified;
    std::uintmax_t bytesBefore = 0U;
    std::uintmax_t bytesAfter = 0U;
    std::vector<std::string> diff;
};

[[nodiscard]] std::string_view KindName(ChangeKind kind) {
    switch (kind) {
    case ChangeKind::Added: return "added";
    case ChangeKind::Deleted: return "deleted";
    case ChangeKind::Modified: break;
    }
    return "modified";
}

std::vector<std::string> SplitLines(const std::string& text) {
    std::vector<std::string> lines;
    std::istringstream stream{ text };
    for (std::string line; std::getline(stream, line);) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        lines.push_back(std::move(line));
    }
    return lines;
}

// One hunk: what differs between the first and the last changed line, with a little context.
std::vector<std::string> LineDiff(const std::string& before, const std::string& after) {
    const std::vector<std::string> a = SplitLines(before);
    const std::vector<std::string> b = SplitLines(after);
    std::size_t prefix = 0U;
    while (prefix < a.size() && prefix < b.size() && a[prefix] == b[prefix]) ++prefix;
    std::size_t suffix = 0U;
    while (suffix < a.size() - prefix && suffix < b.size() - prefix && a[a.size() - 1U - suffix] == b[b.size() - 1U - suffix]) ++suffix;

    const std::size_t start = prefix > kDiffContextLines ? prefix - kDiffContextLines : 0U;
    const std::size_t endA = a.size() - suffix;
    const std::size_t endB = b.size() - suffix;
    const std::size_t tail = std::min(kDiffContextLines, suffix);
    std::vector<std::string> lines;
    lines.push_back("@@ -" + std::to_string(start + 1U) + "," + std::to_string(endA + tail - start) + " +" +
                    std::to_string(start + 1U) + "," + std::to_string(endB + tail - start) + " @@");
    for (std::size_t i = start; i < prefix; ++i) lines.push_back(" " + a[i]);
    for (std::size_t i = prefix; i < endA; ++i) lines.push_back("-" + a[i]);
    for (std::size_t i = prefix; i < endB; ++i) lines.push_back("+" + b[i]);
    for (std::size_t i = 0U; i < tail; ++i) lines.push_back(" " + a[endA + i]);
    if (lines.size() > kDiffLineLimit + 1U) {
        const std::size_t more = lines.size() - (kDiffLineLimit + 1U);
        lines.resize(kDiffLineLimit + 1U);
        lines.push_back("... " + std::to_string(more) + " more lines");
    }
    return lines;
}

// The scene file as `kb_cli scene-list` prints it: the nodes and their components. Empty when it cannot be read.
std::string SceneListing(const CommandRunner& run, const fs::path& scene) {
    if (scene.empty()) return {};
    std::ostringstream out;
    std::ostringstream err;
    try {
        if (run(std::vector<std::string>{ "scene-list", "--scene", scene.generic_string() }, CommandIo{ .out = out, .err = err }) != 0) return {};
    } catch (const std::exception&) {
        return {};
    }
    return out.str();
}

// What differs in a file, as lines: a text diff of UTF-8 text, the nodes of a scene that changed, else
// how many bytes differ.
std::vector<std::string> DiffOf(const CommandRunner& run, const fs::path& beforeFile, const fs::path& afterFile) {
    std::string before;
    std::string after;
    std::error_code ec;
    const auto load = [&ec](const fs::path& file, std::string& content) {
        return file.empty() || (fs::file_size(file, ec) <= kDiffReadLimitBytes && ReadFile(file, content));
    };
    if (!load(beforeFile, before) || !load(afterFile, after)) return { "too large to compare" };
    const auto isText = [](const std::string& text) {
        return text.find(char{ 0 }) == std::string::npos && ToValidUtf8(text) == text;
    };
    if (isText(before) && isText(after)) return LineDiff(before, after);

    const fs::path file = beforeFile.empty() ? afterFile : beforeFile;
    if (file.extension() == ".21kbscene") {
        const std::string nodesBefore = SceneListing(run, beforeFile);
        const std::string nodesAfter = SceneListing(run, afterFile);
        if (nodesBefore != nodesAfter) return LineDiff(nodesBefore, nodesAfter);
    }
    std::size_t differing = 0U;
    std::size_t first = 0U;
    const std::size_t common = std::min(before.size(), after.size());
    for (std::size_t index = 0U; index < common; ++index) {
        if (before[index] == after[index]) continue;
        if (differing == 0U) first = index;
        ++differing;
    }
    if (differing == 0U) return { "binary file: " + std::to_string(before.size()) + " -> " + std::to_string(after.size()) + " bytes" };
    return { "binary file: " + std::to_string(differing) + " byte(s) differ, first at offset " + std::to_string(first) };
}

bool CollectChanges(const CommandRunner& run, const Snapshot& snapshot, std::vector<Change>& changes, std::string& error) {
    Listing current;
    if (!ScanProject(snapshot.root, current, error)) return false;
    for (const auto& [path, size] : current.files) {
        const auto old = snapshot.listing.files.find(path);
        if (old == snapshot.listing.files.end()) {
            changes.push_back(Change{ .path = path, .kind = ChangeKind::Added, .bytesAfter = size,
                .diff = DiffOf(run, {}, snapshot.root / path) });
            continue;
        }
        bool same = old->second == size;
        if (same) {
            std::string before;
            std::string after;
            same = ReadFile(snapshot.backup / path, before) && ReadFile(snapshot.root / path, after) && before == after;
        }
        if (!same) {
            changes.push_back(Change{ .path = path, .kind = ChangeKind::Modified, .bytesBefore = old->second, .bytesAfter = size,
                .diff = DiffOf(run, snapshot.backup / path, snapshot.root / path) });
        }
    }
    for (const auto& [path, size] : snapshot.listing.files) {
        if (!current.files.contains(path)) {
            changes.push_back(Change{ .path = path, .kind = ChangeKind::Deleted, .bytesBefore = size,
                .diff = DiffOf(run, snapshot.backup / path, {}) });
        }
    }
    std::ranges::sort(changes, [](const Change& left, const Change& right) { return left.path < right.path; });
    return true;
}

// Puts every file and folder back as the snapshot has it. Returns false with the first failure.
bool Restore(const Snapshot& snapshot, const std::vector<Change>& changes, std::string& error) {
    std::error_code ec;
    for (const Change& change : changes) {
        const fs::path target = snapshot.root / change.path;
        if (change.kind == ChangeKind::Added) {
            fs::remove(target, ec);
        } else {
            fs::create_directories(target.parent_path(), ec);
            fs::copy_file(snapshot.backup / change.path, target, fs::copy_options::overwrite_existing, ec);
        }
        if (ec) {
            error = "cannot restore " + change.path.generic_string() + ": " + ec.message();
            return false;
        }
    }
    for (const fs::path& directory : snapshot.listing.directories) {
        fs::create_directories(snapshot.root / directory, ec);
    }
    // Folders the operations made: deepest first, and only when they are empty now.
    Listing current;
    std::string scanError;
    if (!ScanProject(snapshot.root, current, scanError)) {
        error = scanError;
        return false;
    }
    std::vector<fs::path> created;
    for (const fs::path& directory : current.directories) {
        if (!snapshot.listing.directories.contains(directory)) created.push_back(directory);
    }
    std::ranges::sort(created, [](const fs::path& left, const fs::path& right) {
        return left.generic_string().size() > right.generic_string().size();
    });
    for (const fs::path& directory : created) fs::remove(snapshot.root / directory, ec);
    return true;
}

// One operation of the batch: a kb_cli command line, split into words.
bool Tokenize(std::string_view line, std::vector<std::string>& words, std::string& error) {
    std::string current;
    bool inWord = false;
    char quote = '\0';
    for (const char character : line) {
        if (quote != '\0') {
            if (character == quote) quote = '\0';
            else current += character;
        } else if (character == '"' || character == '\'') {
            quote = character;
            inWord = true;
        } else if (character == ' ' || character == '\t' || character == '\r') {
            if (inWord) words.push_back(std::move(current));
            current.clear();
            inWord = false;
        } else {
            current += character;
            inWord = true;
        }
    }
    if (quote != '\0') {
        error = "unterminated quote in: " + std::string{ line };
        return false;
    }
    if (inWord) words.push_back(std::move(current));
    return true;
}

// Text: one command per line, # starts a comment, "double" or 'single' quotes keep spaces together.
// JSON: an array of operations, each an array of words or a command line string.
bool LoadOperations(const fs::path& file, std::vector<std::vector<std::string>>& operations, std::string& error) {
    std::string text;
    if (!ReadFile(file, text)) {
        error = "cannot read " + file.generic_string();
        return false;
    }
    if (text.starts_with("\xEF\xBB\xBF")) text.erase(0U, 3U);
    const auto firstWord = text.find_first_not_of(" \t\r\n");
    if (firstWord != std::string::npos && text[firstWord] == '[') {
        JsonValue document;
        std::string parseError;
        if (!JsonValue::Parse(text, document, parseError) || document.GetKind() != JsonValue::Kind::Array) {
            error = file.generic_string() + " is not a JSON array of operations: " + parseError;
            return false;
        }
        for (std::size_t index = 0U; index < document.Size(); ++index) {
            const JsonValue& entry = *document.At(index);
            std::vector<std::string> words;
            if (entry.GetKind() == JsonValue::Kind::String) {
                if (!Tokenize(entry.AsString(), words, error)) return false;
            } else if (entry.GetKind() == JsonValue::Kind::Array) {
                for (std::size_t word = 0U; word < entry.Size(); ++word) {
                    if (entry.At(word)->GetKind() != JsonValue::Kind::String) {
                        error = "operation " + std::to_string(index + 1U) + " must hold strings only";
                        return false;
                    }
                    words.push_back(entry.At(word)->AsString());
                }
            } else {
                error = "operation " + std::to_string(index + 1U) + " must be an array of words or a command line";
                return false;
            }
            if (!words.empty()) operations.push_back(std::move(words));
        }
        return true;
    }
    std::istringstream stream{ text };
    for (std::string line; std::getline(stream, line);) {
        const auto begin = line.find_first_not_of(" \t\r");
        if (begin == std::string::npos || line[begin] == '#') continue;
        std::vector<std::string> words;
        if (!Tokenize(line, words, error)) return false;
        if (!words.empty()) operations.push_back(std::move(words));
    }
    return true;
}

std::string Join(const std::vector<std::string>& words) {
    std::string line;
    for (const std::string& word : words) {
        if (!line.empty()) line += ' ';
        const bool quoted = word.empty() || word.find_first_of(" \t") != std::string::npos;
        line += quoted ? '"' + word + '"' : word;
    }
    return line;
}

struct OperationResult {
    std::string line;
    int exitCode = 0;
    std::string output;
    std::string errors;
};

JsonValue Lines(const std::string& text) {
    JsonValue lines = JsonValue::MakeArray();
    for (const std::string& line : SplitLines(text)) {
        if (!line.empty()) lines.Append(JsonValue::MakeString(ToValidUtf8(line)));
    }
    return lines;
}

struct BatchReport {
    bool dryRun = false;
    bool applied = false;
    bool rolledBack = false;
    std::size_t failedOperation = 0U;
    std::vector<OperationResult> operations;
    std::vector<Change> changes;
    std::vector<std::string> errors;
    fs::path keptBackup;
};

void PrintText(const BatchReport& report, CommandIo io) {
    for (std::size_t index = 0U; index < report.operations.size(); ++index) {
        const OperationResult& operation = report.operations[index];
        io.out << '[' << index + 1U << '/' << report.operations.size() << "] " << operation.line
               << (operation.exitCode == 0 ? "" : "  FAILED") << '\n';
        for (const std::string& line : SplitLines(operation.output)) io.out << "    " << line << '\n';
        for (const std::string& line : SplitLines(operation.errors)) io.err << "    " << line << '\n';
    }
    for (const Change& change : report.changes) {
        io.out << KindName(change.kind) << ' ' << change.path.generic_string() << " (" << change.bytesBefore << " -> "
               << change.bytesAfter << " bytes)\n";
        for (const std::string& line : change.diff) io.out << "    " << line << '\n';
    }
    for (const std::string& error : report.errors) io.err << "error: " << error << '\n';
    if (!report.keptBackup.empty()) {
        io.out << "failed: the files could not all be restored\n";
    } else if (!report.errors.empty()) {
        io.out << "failed: no file was changed\n";
    } else if (report.failedOperation != 0U) {
        io.out << "rolled back: operation " << report.failedOperation << " failed, no file was changed\n";
    } else if (report.dryRun) {
        io.out << "dry run: " << report.changes.size() << " file(s) would change, nothing was written\n";
    } else {
        io.out << "applied: " << report.operations.size() << " operation(s), " << report.changes.size() << " file(s) changed\n";
    }
}

void PrintJson(const BatchReport& report, CommandIo io) {
    JsonValue result = JsonValue::MakeObject();
    result.Set("schema", JsonValue::MakeString("21kb.batch/v1"));
    result.Set("ok", JsonValue::MakeBool(report.errors.empty() && report.failedOperation == 0U));
    result.Set("dryRun", JsonValue::MakeBool(report.dryRun));
    result.Set("applied", JsonValue::MakeBool(report.applied));
    result.Set("rolledBack", JsonValue::MakeBool(report.rolledBack));
    result.Set("failedOperation", JsonValue::MakeNumber(static_cast<double>(report.failedOperation)));
    JsonValue operations = JsonValue::MakeArray();
    for (std::size_t index = 0U; index < report.operations.size(); ++index) {
        const OperationResult& operation = report.operations[index];
        JsonValue row = JsonValue::MakeObject();
        row.Set("index", JsonValue::MakeNumber(static_cast<double>(index + 1U)));
        row.Set("command", JsonValue::MakeString(ToValidUtf8(operation.line)));
        row.Set("exitCode", JsonValue::MakeNumber(operation.exitCode));
        row.Set("output", JsonValue::MakeString(ToValidUtf8(operation.output)));
        row.Set("errors", Lines(operation.errors));
        operations.Append(std::move(row));
    }
    result.Set("operations", std::move(operations));
    JsonValue changes = JsonValue::MakeArray();
    for (const Change& change : report.changes) {
        JsonValue row = JsonValue::MakeObject();
        row.Set("path", JsonValue::MakeString(ToValidUtf8(change.path.generic_string())));
        row.Set("status", JsonValue::MakeString(std::string{ KindName(change.kind) }));
        row.Set("bytesBefore", JsonValue::MakeNumber(static_cast<double>(change.bytesBefore)));
        row.Set("bytesAfter", JsonValue::MakeNumber(static_cast<double>(change.bytesAfter)));
        JsonValue diff = JsonValue::MakeArray();
        for (const std::string& line : change.diff) diff.Append(JsonValue::MakeString(line));
        row.Set("diff", std::move(diff));
        changes.Append(std::move(row));
    }
    result.Set("changes", std::move(changes));
    JsonValue errors = JsonValue::MakeArray();
    for (const std::string& error : report.errors) errors.Append(JsonValue::MakeString(ToValidUtf8(error)));
    result.Set("errors", std::move(errors));
    io.out << result.Dump() << '\n';
}

} // namespace

int RunBatchCommand(const ArgumentList& arguments, CommandIo io, const CommandRunner& run) {
    const auto project = arguments.Option("--project");
    const auto file = arguments.Option("--file");
    if (!project || !file) {
        io.err << "error: batch needs --project <dir> and --file <operations file>\n";
        return 1;
    }
    std::error_code ec;
    const fs::path root = fs::weakly_canonical(fs::path{ *project }, ec);
    if (ec || !fs::is_directory(root, ec)) {
        io.err << "error: --project is not a folder: " << *project << '\n';
        return 1;
    }
    std::vector<std::vector<std::string>> operations;
    std::string error;
    if (!LoadOperations(fs::path{ *file }, operations, error)) {
        io.err << "error: " << error << '\n';
        return 1;
    }
    if (operations.empty()) {
        io.err << "error: " << *file << " has no operations\n";
        return 1;
    }
    // Every operation works on this project, and none may start a batch of its own.
    for (std::size_t index = 0U; index < operations.size(); ++index) {
        std::vector<std::string>& words = operations[index];
        if (words.front() == "kb_cli") words.erase(words.begin());
        const std::string where = "operation " + std::to_string(index + 1U) + ": ";
        if (words.empty() || words.front() == "batch" || words.front() == "mcp" || words.front().starts_with("-")) {
            io.err << "error: " << where << "expects a kb_cli command other than batch and mcp\n";
            return 1;
        }
        const auto given = std::ranges::find(words, "--project");
        if (given == words.end()) {
            words.emplace_back("--project");
            words.push_back(*project);
        } else if (std::next(given) == words.end() ||
                   !fs::equivalent(fs::path{ *std::next(given) }, root, ec)) {
            io.err << "error: " << where << "--project must be the project of the batch\n";
            return 1;
        }
    }

    BatchReport report;
    report.dryRun = arguments.Flag("--dry-run");
    Snapshot snapshot{ .root = root, .backup = fs::temp_directory_path(ec) / ("21kb-batch-" + std::to_string(
        static_cast<std::uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count()))) };
    bool snapshotTaken = false;
    try {
        snapshotTaken = TakeSnapshot(snapshot, error);
        if (!snapshotTaken) report.errors.push_back(error);
        for (std::size_t index = 0U; snapshotTaken && index < operations.size(); ++index) {
            OperationResult result{ .line = Join(operations[index]) };
            std::ostringstream out;
            std::ostringstream err;
            try {
                result.exitCode = run(operations[index], CommandIo{ .out = out, .err = err });
            } catch (const std::exception& exception) {
                err << "error: " << exception.what() << '\n';
                result.exitCode = 1;
            }
            result.output = out.str();
            result.errors = err.str();
            report.operations.push_back(std::move(result));
            if (report.operations.back().exitCode != 0) {
                report.failedOperation = index + 1U;
                break;
            }
        }
    } catch (const std::exception& exception) {
        report.errors.push_back(exception.what());
    }

    if (snapshotTaken) {
        std::string changesError;
        const bool listed = CollectChanges(run, snapshot, report.changes, changesError);
        if (!listed) report.errors.push_back(changesError);
        const bool failed = !report.errors.empty() || report.failedOperation != 0U;
        if (failed || report.dryRun) {
            std::string restoreError;
            if (listed && Restore(snapshot, report.changes, restoreError)) {
                report.rolledBack = true;
            } else {
                report.errors.push_back(listed ? restoreError : "the files could not be compared, so they were not restored");
                report.errors.push_back("the original files are kept in " + snapshot.backup.generic_string());
                report.keptBackup = snapshot.backup;
            }
        } else {
            report.applied = true;
        }
        if (report.keptBackup.empty()) fs::remove_all(snapshot.backup, ec);
    }
    if (report.failedOperation != 0U && report.rolledBack) report.changes.clear();

    if (arguments.Flag("--json")) PrintJson(report, io);
    else PrintText(report, io);
    return report.errors.empty() && report.failedOperation == 0U ? 0 : 1;
}

} // namespace kb::cli
