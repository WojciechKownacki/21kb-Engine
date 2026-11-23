#include "engine/project/AgentUndoJournal.hpp"

#include <algorithm>
#include <array>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace kb::project {
namespace {

namespace fs = std::filesystem;

// A file larger than this is not kept in the snapshot, and the snapshot as a whole stops growing at the total.
constexpr std::uintmax_t kFileLimit = 48ULL << 20U;
constexpr std::uintmax_t kTotalLimit = 1ULL << 30U;
constexpr std::string_view kHeader = "21kb-agent-undo 1";

struct Entry {
    std::uintmax_t size = 0U;
    std::int64_t time = 0;
    // Whether the snapshot holds a copy of the file.
    bool mirrored = false;
};

struct State {
    bool valid = false;
    bool open = false;
    std::string label;
    std::map<std::string, Entry> files;
    std::set<std::string> directories;
};

[[nodiscard]] std::string Utf8(const fs::path& path) {
    const std::u8string text = path.generic_u8string();
    return std::string{ reinterpret_cast<const char*>(text.data()), text.size() };
}

[[nodiscard]] fs::path FromUtf8(const std::string& text) {
    return fs::path{ std::u8string{ reinterpret_cast<const char8_t*>(text.data()), text.size() } };
}

// Folders of generated or foreign data: not part of the project an agent edits, never listed or restored.
[[nodiscard]] bool Excluded(const fs::path& relative) {
    if (relative.empty()) return false;
    const std::string first = relative.begin()->generic_string();
    return first == "Binaries" || first == "Intermediate" || first == "Saved" || first == "Library" || first == "Build" ||
        first == "Saves" || first == ".git" || first == ".vs" || first == ".kb";
}

struct Listing {
    std::map<std::string, Entry> files;
    std::set<std::string> directories;
};

[[nodiscard]] bool Scan(const fs::path& root, Listing& listing, std::string& error) {
    std::error_code ec;
    fs::recursive_directory_iterator it{ root, fs::directory_options::none, ec };
    for (; !ec && it != fs::recursive_directory_iterator{}; it.increment(ec)) {
        const fs::path relative = it->path().lexically_relative(root);
        if (Excluded(relative)) {
            it.disable_recursion_pending();
            continue;
        }
        std::error_code statusError;
        const fs::file_status status = it->symlink_status(statusError);
        // A link may lead out of the project: it is neither followed nor tracked.
        if (statusError || fs::is_symlink(status)) continue;
        if (fs::is_directory(status)) {
            listing.directories.insert(Utf8(relative));
        } else if (fs::is_regular_file(status)) {
            Entry entry;
            entry.size = it->file_size(statusError);
            if (statusError) continue;
            entry.time = static_cast<std::int64_t>(it->last_write_time(statusError).time_since_epoch().count());
            if (statusError) continue;
            listing.files[Utf8(relative)] = entry;
        }
    }
    if (ec) {
        error = "cannot read the project folder: " + ec.message();
        return false;
    }
    return true;
}

[[nodiscard]] std::string Escape(std::string_view text) {
    std::string out;
    for (const char c : text) {
        if (c == '%') out += "%25";
        else if (c == '\t') out += "%09";
        else if (c == '\n') out += "%0A";
        else if (c == '\r') out += "%0D";
        else out += c;
    }
    return out;
}

[[nodiscard]] std::string Unescape(std::string_view text) {
    std::string out;
    for (std::size_t index = 0U; index < text.size(); ++index) {
        if (text[index] == '%' && index + 2U < text.size() + 0U) {
            const std::string code{ text.substr(index + 1U, 2U) };
            if (code == "25") { out += '%'; index += 2U; continue; }
            if (code == "09") { out += '\t'; index += 2U; continue; }
            if (code == "0A") { out += '\n'; index += 2U; continue; }
            if (code == "0D") { out += '\r'; index += 2U; continue; }
        }
        out += text[index];
    }
    return out;
}

[[nodiscard]] fs::path StateFile(const fs::path& root) { return root / "Saved" / "Agent" / "Undo" / "state.txt"; }
[[nodiscard]] fs::path ShadowRoot(const fs::path& root) { return root / "Saved" / "Agent" / "Undo" / "files"; }

// A path the snapshot may name: inside the project, never absolute, never climbing out.
[[nodiscard]] bool SafeRelative(const std::string& text) {
    if (text.empty()) return false;
    const fs::path path = FromUtf8(text);
    if (path.is_absolute() || path.has_root_name() || path.has_root_directory()) return false;
    for (const fs::path& part : path) {
        const std::string name = part.generic_string();
        if (name.empty() || name == ".." || name == "." || name.find(':') != std::string::npos) return false;
    }
    return !Excluded(path);
}

// Whether `target` lies in the project even after links on the way are resolved.
[[nodiscard]] bool InsideProject(const fs::path& root, const fs::path& target) {
    std::error_code ec;
    const fs::path canonicalRoot = fs::weakly_canonical(root, ec);
    if (ec) return false;
    const fs::path canonicalTarget = fs::weakly_canonical(target, ec);
    if (ec) return false;
    const fs::path relative = canonicalTarget.lexically_relative(canonicalRoot);
    return !relative.empty() && *relative.begin() != "..";
}

[[nodiscard]] State Load(const fs::path& root, bool headerOnly = false) {
    State state;
    std::ifstream input{ StateFile(root), std::ios::binary };
    if (!input) return state;
    std::string line;
    if (!std::getline(input, line) || line != kHeader) return state;
    bool valid = false;
    while (std::getline(input, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.starts_with("valid ")) { valid = line == "valid 1"; continue; }
        if (line.starts_with("open ")) { state.open = line == "open 1"; continue; }
        if (line.starts_with("label ")) { state.label = Unescape(std::string_view{ line }.substr(6U)); continue; }
        // The header (valid, open, label) comes first: the folders and files after it are not read when only it is asked for.
        if (headerOnly && (line.starts_with("D\t") || line.starts_with("F\t"))) break;
        // A record made when more folders counted as the project's own may still name some that do not any more.
        if (line.starts_with("D\t")) {
            std::string directory = Unescape(std::string_view{ line }.substr(2U));
            if (!Excluded(FromUtf8(directory))) state.directories.insert(std::move(directory));
            continue;
        }
        if (line.starts_with("F\t")) {
            // F <size> <time> <mirrored> <path>
            std::array<std::string, 3> fields;
            std::size_t start = 2U;
            bool ok = true;
            for (std::size_t field = 0U; field < 3U; ++field) {
                const std::size_t end = line.find('\t', start);
                if (end == std::string::npos) { ok = false; break; }
                fields[field] = line.substr(start, end - start);
                start = end + 1U;
            }
            if (!ok) continue;
            Entry entry;
            try {
                entry.size = std::stoull(fields[0]);
                entry.time = std::stoll(fields[1]);
            } catch (const std::exception&) {
                continue;
            }
            entry.mirrored = fields[2] == "1";
            std::string path = Unescape(std::string_view{ line }.substr(start));
            if (!Excluded(FromUtf8(path))) state.files[std::move(path)] = entry;
        }
    }
    state.valid = valid;
    return state;
}

[[nodiscard]] bool Save(const fs::path& root, const State& state, std::string& error) {
    const fs::path file = StateFile(root);
    std::error_code ec;
    fs::create_directories(file.parent_path(), ec);
    const fs::path temporary = fs::path{ file }.concat(".tmp");
    {
        std::ofstream output{ temporary, std::ios::binary | std::ios::trunc };
        if (!output) {
            error = "cannot write the undo record";
            return false;
        }
        output << kHeader << '\n' << "valid " << (state.valid ? 1 : 0) << '\n' << "open " << (state.open ? 1 : 0) << '\n'
               << "label " << Escape(state.label) << '\n';
        for (const std::string& directory : state.directories) output << "D\t" << Escape(directory) << '\n';
        for (const auto& [path, entry] : state.files) {
            output << "F\t" << entry.size << '\t' << entry.time << '\t' << (entry.mirrored ? 1 : 0) << '\t' << Escape(path) << '\n';
        }
        output.flush();
        if (!output) {
            error = "cannot write the undo record";
            return false;
        }
    }
    fs::rename(temporary, file, ec);
    if (ec) {
        error = "cannot write the undo record: " + ec.message();
        return false;
    }
    return true;
}

[[nodiscard]] bool SameBytes(const fs::path& left, const fs::path& right) {
    std::ifstream first{ left, std::ios::binary };
    std::ifstream second{ right, std::ios::binary };
    if (!first || !second) return false;
    std::vector<char> firstBlock(1U << 16U);
    std::vector<char> secondBlock(firstBlock.size());
    for (;;) {
        first.read(firstBlock.data(), static_cast<std::streamsize>(firstBlock.size()));
        second.read(secondBlock.data(), static_cast<std::streamsize>(secondBlock.size()));
        const std::streamsize count = first.gcount();
        if (count != second.gcount() || !std::equal(firstBlock.begin(), firstBlock.begin() + count, secondBlock.begin())) return false;
        if (count < static_cast<std::streamsize>(firstBlock.size())) return !first.bad() && !second.bad();
    }
}

[[nodiscard]] fs::file_time_type TimeOf(std::int64_t ticks) {
    return fs::file_time_type{ fs::file_time_type::duration{ ticks } };
}

void RemoveEmptyParents(const fs::path& from, const fs::path& stop) {
    std::error_code ec;
    for (fs::path folder = from; folder != stop && folder.has_parent_path() && folder != folder.parent_path(); folder = folder.parent_path()) {
        if (!fs::is_empty(folder, ec) || ec) return;
        fs::remove(folder, ec);
        if (ec) return;
    }
}

} // namespace

AgentUndoJournal::AgentUndoJournal(fs::path projectRoot) : root_(std::move(projectRoot)) {}

fs::path AgentUndoJournal::Folder() const {
    return root_ / "Saved" / "Agent" / "Undo";
}

bool AgentUndoJournal::Open() const {
    const State state = Load(root_, true);
    return state.valid && state.open;
}

std::string AgentUndoJournal::Label() const {
    return Load(root_, true).label;
}

bool AgentUndoJournal::Begin(std::string_view label, AgentSnapshotInfo* info, std::string& error) {
    std::error_code ec;
    if (!fs::is_directory(root_, ec)) {
        error = "the project folder does not exist";
        return false;
    }
    State previous = Load(root_);
    const fs::path shadow = ShadowRoot(root_);
    // A record that was cut short cannot be trusted: its mirror is made again from nothing.
    if (!previous.valid) {
        fs::remove_all(shadow, ec);
        previous.files.clear();
    }
    State marker;
    if (!Save(root_, marker, error)) return false;

    Listing now;
    if (!Scan(root_, now, error)) return false;

    State next;
    next.valid = true;
    next.open = true;
    next.label = std::string{ label };
    next.directories = now.directories;
    AgentSnapshotInfo summary;
    std::uintmax_t total = 0U;
    for (const auto& [path, entry] : now.files) {
        ++summary.files;
        Entry kept = entry;
        const auto before = previous.files.find(path);
        const fs::path copy = shadow / FromUtf8(path);
        if (before != previous.files.end() && before->second.mirrored && before->second.size == entry.size && before->second.time == entry.time &&
            fs::is_regular_file(copy, ec)) {
            kept.mirrored = true;
            total += entry.size;
            next.files[path] = kept;
            continue;
        }
        kept.mirrored = false;
        if (entry.size <= kFileLimit && total + entry.size <= kTotalLimit) {
            fs::create_directories(copy.parent_path(), ec);
            const bool copied = !ec && fs::copy_file(root_ / FromUtf8(path), copy, fs::copy_options::overwrite_existing, ec) && !ec;
            if (copied) {
                kept.mirrored = true;
                total += entry.size;
                ++summary.copied;
            }
        }
        if (!kept.mirrored) {
            fs::remove(copy, ec);
            ++summary.unprotected;
        }
        next.files[path] = kept;
    }
    // Files that are gone from the project are gone from the mirror.
    for (const auto& [path, entry] : previous.files) {
        if (now.files.contains(path)) continue;
        const fs::path copy = shadow / FromUtf8(path);
        fs::remove(copy, ec);
        RemoveEmptyParents(copy.parent_path(), shadow);
    }
    if (!Save(root_, next, error)) return false;
    if (info != nullptr) *info = summary;
    return true;
}

bool AgentUndoJournal::Changes(std::vector<AgentFileChange>& changes, std::string& error) const {
    changes.clear();
    const State state = Load(root_);
    // A snapshot that was kept or undone has nothing left to decide.
    if (!state.valid || !state.open) return true;
    Listing now;
    if (!Scan(root_, now, error)) return false;
    const fs::path shadow = ShadowRoot(root_);
    for (const auto& [path, entry] : now.files) {
        const auto before = state.files.find(path);
        if (before == state.files.end()) {
            changes.push_back({ path, AgentChangeKind::Added, 0U, entry.size, true });
            continue;
        }
        if (before->second.size == entry.size && before->second.time == entry.time) continue;
        // Written again with the same bytes (a save that changed nothing) is no change.
        if (before->second.mirrored && before->second.size == entry.size && SameBytes(shadow / FromUtf8(path), root_ / FromUtf8(path))) continue;
        changes.push_back({ path, AgentChangeKind::Modified, before->second.size, entry.size, before->second.mirrored });
    }
    for (const auto& [path, entry] : state.files) {
        if (!now.files.contains(path)) changes.push_back({ path, AgentChangeKind::Deleted, entry.size, 0U, entry.mirrored });
    }
    std::ranges::sort(changes, [](const AgentFileChange& left, const AgentFileChange& right) { return left.path < right.path; });
    return true;
}

bool AgentUndoJournal::Undo(std::span<const std::string> paths, std::vector<std::string>& restored, std::string& error) {
    restored.clear();
    State state = Load(root_);
    if (!state.valid) {
        error = "there is no snapshot to undo to";
        return false;
    }
    std::vector<AgentFileChange> changes;
    if (!Changes(changes, error)) return false;
    const bool everything = paths.empty();
    std::vector<AgentFileChange> chosen;
    if (everything) {
        chosen = changes;
    } else {
        for (const std::string& wanted : paths) {
            const auto found = std::ranges::find_if(changes, [&wanted](const AgentFileChange& change) { return change.path == wanted; });
            if (found == changes.end()) {
                error = "no change to undo in '" + wanted + "'";
                if (!changes.empty()) {
                    error += "; changed files:";
                    for (std::size_t index = 0U; index < changes.size() && index < 8U; ++index) error += " " + changes[index].path;
                }
                return false;
            }
            chosen.push_back(*found);
        }
    }

    const fs::path shadow = ShadowRoot(root_);
    std::size_t unrestorable = 0U;
    std::error_code ec;
    for (const AgentFileChange& change : chosen) {
        if (!SafeRelative(change.path)) {
            error = "refused to touch '" + change.path + "': it is not a file of the project";
            return false;
        }
        const fs::path target = root_ / FromUtf8(change.path);
        if (!InsideProject(root_, target)) {
            error = "refused to touch '" + change.path + "': it leads outside the project";
            return false;
        }
        if (change.kind == AgentChangeKind::Added) {
            fs::remove(target, ec);
            if (ec) {
                error = "cannot remove " + change.path + ": " + ec.message();
                return false;
            }
            restored.push_back(change.path);
            continue;
        }
        if (!change.restorable) {
            ++unrestorable;
            continue;
        }
        const fs::path copy = shadow / FromUtf8(change.path);
        if (!fs::is_regular_file(copy, ec)) {
            error = "the saved copy of " + change.path + " is missing";
            return false;
        }
        fs::create_directories(target.parent_path(), ec);
        const fs::path temporary = fs::path{ target }.concat(".kbundo");
        fs::copy_file(copy, temporary, fs::copy_options::overwrite_existing, ec);
        if (!ec) fs::rename(temporary, target, ec);
        if (ec) {
            fs::remove(temporary, ec);
            error = "cannot restore " + change.path + ": " + ec.message();
            return false;
        }
        // The file is as it was, including when it was written, so the snapshot still matches it.
        fs::last_write_time(target, TimeOf(state.files.at(change.path).time), ec);
        restored.push_back(change.path);
    }

    if (everything) {
        // Folders the agent made go, folders it removed come back.
        Listing now;
        if (Scan(root_, now, error)) {
            std::vector<std::string> added;
            for (const std::string& directory : now.directories)
                if (!state.directories.contains(directory)) added.push_back(directory);
            std::ranges::sort(added, [](const std::string& left, const std::string& right) { return left.size() > right.size(); });
            for (const std::string& directory : added) {
                if (SafeRelative(directory) && InsideProject(root_, root_ / FromUtf8(directory))) fs::remove(root_ / FromUtf8(directory), ec);
            }
            for (const std::string& directory : state.directories) {
                if (SafeRelative(directory) && InsideProject(root_, root_ / FromUtf8(directory))) fs::create_directories(root_ / FromUtf8(directory), ec);
            }
        }
        error.clear();
    }

    std::vector<AgentFileChange> left;
    std::string leftError;
    if (Changes(left, leftError) && left.empty()) {
        state.open = false;
        if (!Save(root_, state, error)) return false;
    }
    if (unrestorable != 0U) {
        error = std::to_string(unrestorable) + " large file(s) were changed but were too large to keep a copy of, so they stay as they are";
        return false;
    }
    error.clear();
    return true;
}

bool AgentUndoJournal::Keep(std::string& error) {
    State state = Load(root_);
    if (!state.valid) return true;
    state.open = false;
    return Save(root_, state, error);
}

} // namespace kb::project
