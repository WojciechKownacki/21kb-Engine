#include "TestSupport.hpp"
#include "TestSuites.hpp"

#include "engine/project/AgentProcessMarker.hpp"
#include "engine/project/AgentRequestContext.hpp"
#include "engine/project/AgentUndoJournal.hpp"

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <system_error>
#include <vector>

namespace {

namespace fs = std::filesystem;
using kb::project::AgentChangeKind;
using kb::project::AgentFileChange;
using kb::project::AgentUndoJournal;

void Check(bool condition, const std::string& message) {
    kb::tests::Require(condition, message.c_str());
}

[[nodiscard]] fs::path JournalRoot() {
    return fs::temp_directory_path() / "21kb_engine_agent_undo_tests";
}

void Write(const fs::path& file, const std::string& text) {
    std::error_code error;
    fs::create_directories(file.parent_path(), error);
    std::ofstream output{ file, std::ios::binary | std::ios::trunc };
    output << text;
    Check(output.good(), "agent undo fixture could not be written");
}

[[nodiscard]] std::string Read(const fs::path& file) {
    std::ifstream input{ file, std::ios::binary };
    return std::string{ std::istreambuf_iterator<char>{ input }, std::istreambuf_iterator<char>{} };
}

[[nodiscard]] const AgentFileChange* Find(const std::vector<AgentFileChange>& changes, const std::string& path) {
    const auto found = std::ranges::find_if(changes, [&path](const AgentFileChange& change) { return change.path == path; });
    return found == changes.end() ? nullptr : &*found;
}

void MakeProject(const fs::path& root) {
    std::error_code error;
    fs::remove_all(root, error);
    Write(root / "Game.21kbproject", "project");
    Write(root / "Assets" / "Scenes" / "Main.21kbscene", std::string("binary\0scene\xff\x01", 14));
    Write(root / "Assets" / "Scripts" / "Spin.lua", "function OnUpdate() end\n");
    Write(root / "Assets" / "Materials" / "Red.kbmat", "{\"color\":\"red\"}");
    Write(root / "Saved" / "Cache" / "ignored.bin", "not part of the project");
}

void RunAgentUndoJournalTest() {
    const fs::path root = JournalRoot();
    MakeProject(root);
    std::string error;

    AgentUndoJournal journal{ root };
    Check(!journal.Open(), "a journal without a snapshot must not be open");
    std::vector<AgentFileChange> changes;
    Check(journal.Changes(changes, error) && changes.empty(), "no snapshot means no changes");

    kb::project::AgentSnapshotInfo info;
    Check(journal.Begin("add a cube", &info, error), "the first snapshot must succeed: " + error);
    Check(info.files == 4U && info.copied == 4U && info.unprotected == 0U, "the first snapshot copies every project file once");
    Check(journal.Open() && journal.Label() == "add a cube", "the snapshot is open and keeps its label");
    Check(journal.Changes(changes, error) && changes.empty(), "a fresh snapshot has no changes");

    // The agent changes a file, writes one in a new folder, deletes two, and a tool writes to Saved.
    const std::string sceneBefore = Read(root / "Assets" / "Scenes" / "Main.21kbscene");
    Write(root / "Assets" / "Scripts" / "Spin.lua", "function OnUpdate() Log('spin') end\n");
    Write(root / "Assets" / "Generated" / "Coin.lua", "-- new\n");
    fs::remove(root / "Assets" / "Scenes" / "Main.21kbscene");
    fs::remove(root / "Assets" / "Materials" / "Red.kbmat");
    Write(root / "Saved" / "Cache" / "ignored.bin", "changed but not tracked");

    Check(journal.Changes(changes, error), "changes must be listed: " + error);
    Check(changes.size() == 4U, "exactly the four project files changed");
    Check(Find(changes, "Assets/Scripts/Spin.lua") != nullptr && Find(changes, "Assets/Scripts/Spin.lua")->kind == AgentChangeKind::Modified, "the edit is Modified");
    Check(Find(changes, "Assets/Generated/Coin.lua") != nullptr && Find(changes, "Assets/Generated/Coin.lua")->kind == AgentChangeKind::Added, "the new file is Added");
    Check(Find(changes, "Assets/Scenes/Main.21kbscene") != nullptr && Find(changes, "Assets/Scenes/Main.21kbscene")->kind == AgentChangeKind::Deleted, "the binary is Deleted");
    Check(Find(changes, "Assets/Materials/Red.kbmat") != nullptr && Find(changes, "Assets/Materials/Red.kbmat")->restorable, "a deleted file can be restored");
    Check(Find(changes, "Saved/Cache/ignored.bin") == nullptr, "Saved is not part of the project's files");

    // Undo of one file touches that file only.
    std::vector<std::string> restored;
    const std::vector<std::string> one{ "Assets/Scripts/Spin.lua" };
    Check(journal.Undo(one, restored, error) && restored == one, "one file is put back: " + error);
    Check(Read(root / "Assets" / "Scripts" / "Spin.lua") == "function OnUpdate() end\n", "the edited file has its old text");
    Check(fs::exists(root / "Assets" / "Generated" / "Coin.lua") && !fs::exists(root / "Assets" / "Scenes" / "Main.21kbscene"),
        "the other changes stay while one file is undone");
    Check(journal.Open(), "changes remain, so the snapshot stays open");
    Check(!journal.Undo(std::vector<std::string>{ "Assets/Nothing.txt" }, restored, error) && error.find("Assets/Nothing.txt") != std::string::npos,
        "a file with no change is named in the error");

    // A new journal object finds the same record: it survives a restart of the editor.
    AgentUndoJournal reopened{ root };
    Check(reopened.Open() && reopened.Label() == "add a cube", "the record survives a restart");
    Check(reopened.Changes(changes, error) && changes.size() == 3U, "three changes are left");

    Check(reopened.Undo({}, restored, error), "undo all must succeed: " + error);
    Check(Read(root / "Assets" / "Scenes" / "Main.21kbscene") == sceneBefore, "the deleted binary comes back byte for byte");
    Check(Read(root / "Assets" / "Materials" / "Red.kbmat") == "{\"color\":\"red\"}", "the deleted material comes back");
    Check(!fs::exists(root / "Assets" / "Generated"), "the folder the agent made is removed with its file");
    Check(!reopened.Open(), "nothing is left to decide after undo all");
    Check(reopened.Changes(changes, error) && changes.empty(), "the project is as it was");
    Check(Read(root / "Saved" / "Cache" / "ignored.bin") == "changed but not tracked", "Saved is left alone");

    // The next snapshot copies only what changed since the last one.
    Write(root / "Assets" / "Scripts" / "Spin.lua", "function OnUpdate() Log('again') end\n");
    Check(reopened.Begin("second", &info, error) && info.files == 4U && info.copied == 1U, "a second snapshot copies only the changed file");
    Check(reopened.Changes(changes, error) && changes.empty(), "the second snapshot starts clean");

    // Writing a file again with the same bytes is not a change; keeping closes the record.
    Write(root / "Assets" / "Materials" / "Red.kbmat", "{\"color\":\"red\"}");
    Check(reopened.Changes(changes, error) && changes.empty(), "an identical rewrite is not a change");
    Write(root / "Assets" / "Materials" / "Red.kbmat", "{\"color\":\"blue\"}");
    Check(reopened.Changes(changes, error) && changes.size() == 1U, "a real edit is a change");
    Check(reopened.Keep(error) && !reopened.Open(), "keeping closes the record");
    Check(Read(root / "Assets" / "Materials" / "Red.kbmat") == "{\"color\":\"blue\"}", "keeping leaves the files as they are");
}

// A record that names a path outside the project is never followed.
void RunAgentUndoJournalConfinementTest() {
    const fs::path root = JournalRoot() / "inner";
    MakeProject(root);
    std::string error;
    AgentUndoJournal journal{ root };
    Check(journal.Begin("x", nullptr, error), "snapshot: " + error);
    const fs::path outside = JournalRoot() / "outside.txt";
    std::error_code ec;
    fs::remove(outside, ec);
    {
        std::ofstream state{ root / "Saved" / "Agent" / "Undo" / "state.txt", std::ios::binary | std::ios::app };
        state << "F\t1\t0\t1\t../outside.txt\n";
    }
    std::vector<AgentFileChange> changes;
    Check(journal.Changes(changes, error) && Find(changes, "../outside.txt") != nullptr, "the tampered record shows up as a change");
    std::vector<std::string> restored;
    Check(!journal.Undo({}, restored, error) && error.find("refused") != std::string::npos, "undo refuses a path that leaves the project");
    Check(!fs::exists(outside), "nothing is written outside the project");
}

// A file too large to keep is listed but never claimed restorable.
void RunAgentUndoJournalLargeFileTest() {
    const fs::path root = JournalRoot() / "large";
    MakeProject(root);
    {
        std::ofstream big{ root / "Assets" / "Big.bin", std::ios::binary | std::ios::trunc };
        const std::string block(1U << 20U, 'x');
        for (int index = 0; index < 50; ++index) big << block;
    }
    std::string error;
    AgentUndoJournal journal{ root };
    kb::project::AgentSnapshotInfo info;
    Check(journal.Begin("big", &info, error) && info.unprotected == 1U, "the large file is reported as not kept");
    {
        std::ofstream big{ root / "Assets" / "Big.bin", std::ios::binary | std::ios::app };
        big << "more";
    }
    std::vector<AgentFileChange> changes;
    Check(journal.Changes(changes, error) && changes.size() == 1U && !changes.front().restorable, "its change is listed as not restorable");
    std::vector<std::string> restored;
    Check(!journal.Undo({}, restored, error) && error.find("too large") != std::string::npos, "undo says the large file stays as it is");
}

// The marker of a process: this one is alive, one that is not running is dead, a file that is no marker is damaged.
void RunAgentProcessMarkerTest() {
    const fs::path root = JournalRoot() / "Marker";
    std::error_code code;
    fs::remove_all(root, code);
    const fs::path file = root / "Saved" / "Agent" / "marker.json";
    using kb::project::AgentMarkerState;
    Check(kb::project::ReadAgentProcessMarker(file).state == AgentMarkerState::Absent, "no file is no marker");
    std::string error;
    Check(kb::project::WriteAgentProcessMarker(file, "backup folder", error), "the marker is written, its folder made: " + error);
    const kb::project::AgentProcessMarker own = kb::project::ReadAgentProcessMarker(file);
    Check(own.state == AgentMarkerState::Alive && own.detail == "backup folder" && own.pid != 0U, "this process is alive");
    Write(file, "{\"pid\":999999,\"started\":1,\"detail\":\"x\"}");
    Check(kb::project::ReadAgentProcessMarker(file).state == AgentMarkerState::Dead, "a process that is not running is dead");
    // The pid of a running process with another start time is a reused pid: dead too.
    Write(file, "{\"pid\":" + std::to_string(own.pid) + ",\"started\":1,\"detail\":\"x\"}");
#if defined(_WIN32)
    Check(kb::project::ReadAgentProcessMarker(file).state == AgentMarkerState::Dead, "a reused pid is dead");
#endif
    Write(file, "not a marker");
    const kb::project::AgentProcessMarker damaged = kb::project::ReadAgentProcessMarker(file);
    Check(damaged.state == AgentMarkerState::Damaged && !damaged.error.empty(), "a file that is not a marker is damaged, with the reason");
    Write(file, "{\"pid\":0,\"started\":1,\"detail\":\"x\"}");
    Check(kb::project::ReadAgentProcessMarker(file).state == AgentMarkerState::Damaged, "a marker without a process is damaged");
}

// What the agent is told with a request: the project's notes, cut at the limit and said so, and one line about the editor.
void RunAgentRequestContextTest() {
    const fs::path root = JournalRoot() / "Context";
    std::error_code code;
    fs::remove_all(root, code);
    Check(kb::project::AgentProjectNotes(root).empty(), "a project without notes sends none");
    Write(root / "AgentNotes.md", "The hero is called Mira.\n");
    const std::string notes = kb::project::AgentProjectNotes(root);
    Check(notes.find("The hero is called Mira.") != std::string::npos && notes.find("was cut") == std::string::npos, "short notes are sent whole");
    std::string longNotes(kb::project::kAgentNotesLimit - 1U, 'a');
    longNotes += "\xC3\xA9" + std::string(100U, 'b');
    Write(root / "AgentNotes.md", longNotes);
    const std::string cut = kb::project::AgentProjectNotes(root);
    Check(cut.find("AgentNotes.md was cut: " + std::to_string(kb::project::kAgentNotesLimit - 1U) + " of " + std::to_string(longNotes.size()) + " bytes shown") != std::string::npos,
        "long notes are cut inside no character, and the cut is named with the sizes: " + cut.substr(cut.size() - 120U));
    Check(cut.find("\xC3") == std::string::npos, "a half character is not sent");

    const std::vector<std::string> none;
    Check(kb::project::AgentEditorContextLine("", none) == "Editor: no scene open, nothing selected.\n\n", "an empty editor says so");
    const std::vector<std::string> two{ "Crate", "Ball" };
    Check(kb::project::AgentEditorContextLine("Assets/Scenes/Main.21kbscene", two) == "Editor: scene Assets/Scenes/Main.21kbscene open, selected: Crate, Ball.\n\n",
        "the scene and the selected objects are named");
    std::vector<std::string> many;
    for (int index = 0; index < 11; ++index) many.push_back("O" + std::to_string(index));
    const std::string long_ = kb::project::AgentEditorContextLine("S", many);
    Check(long_.find("O7 and 3 more.") != std::string::npos && long_.find("O8") == std::string::npos, "a long selection is named in part and counted");
}

} // namespace

namespace kb::tests {

void RunAgentUndoJournalTests() {
    RunAgentUndoJournalTest();
    RunAgentUndoJournalConfinementTest();
    RunAgentUndoJournalLargeFileTest();
    RunAgentProcessMarkerTest();
    RunAgentRequestContextTest();
    std::error_code error;
    fs::remove_all(JournalRoot(), error);
}

} // namespace kb::tests
