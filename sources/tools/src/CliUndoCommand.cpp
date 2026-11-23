#include "CliCommands.hpp"

#include "engine/assets/AssetFileName.hpp"
#include "engine/core/JsonValue.hpp"
#include "engine/project/AgentUndoJournal.hpp"

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

// undo: the undo point of an agent's changes to the project's files (the editor's Agent panel takes one before each request).
// --begin records the project as it is, --list shows what changed since, --last puts the files back (all, or --file ones),
// --accept accepts the changes. The record is in the project's Saved/Agent/Undo folder.

namespace kb::cli {
namespace {

namespace fs = std::filesystem;
using kb::project::AgentChangeKind;
using kb::project::AgentFileChange;

constexpr std::string_view kUsage =
    "usage: kb_cli undo --project <dir> (--begin [--label <text>] | --list | --last [--file <path>]... | --accept)\n";

[[nodiscard]] std::string_view KindName(AgentChangeKind kind) noexcept {
    switch (kind) {
    case AgentChangeKind::Added: return "added";
    case AgentChangeKind::Deleted: return "deleted";
    case AgentChangeKind::Modified: break;
    }
    return "modified";
}

void PublishChanges(CommandIo io, const std::vector<AgentFileChange>& changes, bool open) {
    if (!io.WantsData()) return;
    kb::core::JsonValue data = DataObject("21kb.cli.undo/v1");
    data.Set("open", kb::core::JsonValue::MakeBool(open));
    kb::core::JsonValue list = kb::core::JsonValue::MakeArray();
    for (const AgentFileChange& change : changes) {
        kb::core::JsonValue row = kb::core::JsonValue::MakeObject();
        row.Set("path", kb::core::JsonValue::MakeString(change.path));
        row.Set("status", kb::core::JsonValue::MakeString(std::string{ KindName(change.kind) }));
        row.Set("bytesBefore", kb::core::JsonValue::MakeNumber(static_cast<double>(change.bytesBefore)));
        row.Set("bytesAfter", kb::core::JsonValue::MakeNumber(static_cast<double>(change.bytesAfter)));
        row.Set("restorable", kb::core::JsonValue::MakeBool(change.restorable));
        list.Append(std::move(row));
    }
    data.Set("changes", std::move(list));
    PublishData(io, std::move(data));
}

} // namespace

int RunUndoCommand(const ArgumentList& arguments, CommandIo io) {
    const std::optional<std::string> project = arguments.Option("--project");
    const int modes = static_cast<int>(arguments.Flag("--begin")) + static_cast<int>(arguments.Flag("--list")) +
        static_cast<int>(arguments.Flag("--last")) + static_cast<int>(arguments.Flag("--accept"));
    if (!project.has_value() || modes != 1) {
        io.err << "error: undo needs --project <dir> and exactly one of --begin, --list, --last, --accept\n" << kUsage;
        return 1;
    }
    fs::path root = fs::absolute(kb::assets::Utf8Path(*project)).lexically_normal();
    std::error_code status;
    if (root.has_extension() && fs::is_regular_file(root, status)) root = root.parent_path();
    if (!fs::is_directory(root, status)) {
        io.err << "error: --project is not a folder: " << *project << '\n';
        return 1;
    }
    kb::project::AgentUndoJournal journal{ root };
    std::string error;

    if (arguments.Flag("--begin")) {
        kb::project::AgentSnapshotInfo info;
        if (!journal.Begin(arguments.Option("--label").value_or(std::string{}), &info, error)) {
            io.err << "error: " << error << '\n';
            return 1;
        }
        io.out << "undo point taken: " << info.files << " files, " << info.copied << " copied";
        if (info.unprotected != 0U) io.out << ", " << info.unprotected << " too large to keep (their changes are listed but cannot be undone)";
        io.out << "\nnext: undo --list shows what changed since; undo --last puts it back\n";
        return 0;
    }

    std::vector<AgentFileChange> changes;
    if (arguments.Flag("--list")) {
        if (!journal.Changes(changes, error)) {
            io.err << "error: " << error << '\n';
            return 1;
        }
        PublishChanges(io, changes, journal.Open());
        const std::string label = journal.Label();
        if (changes.empty()) {
            io.out << (journal.Open() ? "no changes since the undo point" : "no undo point with changes (undo --begin takes one)") << '\n';
            return 0;
        }
        io.out << changes.size() << " change(s) since the undo point" << (label.empty() ? std::string{} : " (" + label + ")") << ":\n";
        for (const AgentFileChange& change : changes) {
            io.out << KindName(change.kind) << ' ' << change.path << " (" << change.bytesBefore << " -> " << change.bytesAfter << " bytes)"
                   << (change.restorable ? "" : " [too large to restore]") << '\n';
        }
        io.out << "next: undo --last puts them all back (--file <path> one of them); undo --accept accepts them\n";
        return 0;
    }

    if (arguments.Flag("--accept")) {
        if (!journal.Keep(error)) {
            io.err << "error: " << error << '\n';
            return 1;
        }
        io.out << "changes kept\n";
        return 0;
    }

    // --last
    if (!journal.Changes(changes, error)) {
        io.err << "error: " << error << '\n';
        return 1;
    }
    if (changes.empty()) {
        io.out << "nothing to undo\n";
        return 0;
    }
    const std::vector<std::string> files = arguments.Options("--file");
    std::vector<std::string> restored;
    const bool ok = journal.Undo(files, restored, error);
    for (const std::string& path : restored) io.out << "restored " << path << '\n';
    if (!ok) {
        io.err << "error: " << error << '\n';
        return 1;
    }
    io.out << "undone: " << restored.size() << " file(s) put back\n";
    return 0;
}

} // namespace kb::cli
