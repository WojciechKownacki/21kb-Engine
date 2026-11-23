#pragma once

#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace kb::project {

// What one file of the project is, compared with the moment the journal last took its snapshot.
enum class AgentChangeKind : std::uint8_t {
    Added,
    Modified,
    Deleted,
};

struct AgentFileChange {
    // Relative to the project folder, with '/' separators.
    std::string path;
    AgentChangeKind kind = AgentChangeKind::Modified;
    std::uintmax_t bytesBefore = 0U;
    std::uintmax_t bytesAfter = 0U;
    // False for a file too large to be kept in the snapshot: it is listed, but an undo cannot bring it back.
    bool restorable = true;
};

// What taking a snapshot cost and covered.
struct AgentSnapshotInfo {
    std::size_t files = 0U;
    // Files whose content was copied this time; the others were already in the snapshot unchanged.
    std::size_t copied = 0U;
    // Files left out because they are too large (or the snapshot is full): their changes are listed but cannot be undone.
    std::size_t unprotected = 0U;
};

// An undo point for the changes an agent makes to a project's files. Begin records the project as it is; Changes lists what
// differs from that record (changed, added, deleted files); Undo puts files back exactly, deleted ones included; Keep accepts
// the changes. The record lives in the project's Saved/Agent/Undo folder, so it survives a restart of the editor.
//
// The snapshot is one mirror of the project's files that is brought up to date at each Begin by size and write time, so the
// cost of a snapshot follows what changed since the last one, not the size of the project. Folders of generated or foreign data
// (Binaries, Intermediate, Saved, Library, Build, Saves, .git, .vs, .kb) are not part of it. Nothing outside the project folder is
// ever read for restoring, written or removed.
class AgentUndoJournal final {
public:
    explicit AgentUndoJournal(std::filesystem::path projectRoot);

    // Records the project as it is now, under `label` (what the agent was asked). Keeps an earlier undecided request as accepted.
    [[nodiscard]] bool Begin(std::string_view label, AgentSnapshotInfo* info, std::string& error);

    // Whether a snapshot exists whose changes were neither kept nor undone.
    [[nodiscard]] bool Open() const;
    [[nodiscard]] std::string Label() const;

    // The files that differ from the snapshot, sorted by path. Empty without a snapshot.
    [[nodiscard]] bool Changes(std::vector<AgentFileChange>& changes, std::string& error) const;

    // Puts back the named files (relative paths as Changes gives them), or all changed files when `paths` is empty. `restored`
    // gets the paths that were put back. Closes the snapshot once nothing is left to undo.
    [[nodiscard]] bool Undo(std::span<const std::string> paths, std::vector<std::string>& restored, std::string& error);

    // Accepts the changes: the snapshot is closed and the files stay as they are.
    [[nodiscard]] bool Keep(std::string& error);

    // Where the snapshot lives, inside the project.
    [[nodiscard]] std::filesystem::path Folder() const;

private:
    std::filesystem::path root_;
};

} // namespace kb::project
