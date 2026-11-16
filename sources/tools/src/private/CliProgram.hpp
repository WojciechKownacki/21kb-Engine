#pragma once

#include "CliCommon.hpp"

#include <functional>
#include <istream>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace kb::cli {

enum class CliCommandKind { Normal, Particle, Batch, Mcp, Help, Catalog };

// What --json gives for a command: Envelope wraps its text in the common result object, Native is its own
// versioned JSON, Partial is native for some forms only, Protocol is a JSON protocol on stdin and stdout,
// Structured puts the answer of its read forms in the "data" field of the result object (a schema id inside); a
// command that writes (CliCommandDescriptor::writes) is always Structured: its data lists the changes it made.
enum class CliJsonOutput { Envelope, Native, Partial, Protocol, Structured };

struct CliCommandDescriptor {
    std::string_view name;
    int (*run)(const ArgumentList&, CommandIo) = nullptr;
    CliCommandKind kind = CliCommandKind::Normal;
    bool available = true;
    std::string_view mcpTool;
    CliJsonOutput json = CliJsonOutput::Envelope;
    // Some form changes files: with --json its data holds "changes", every file it created, modified or removed.
    bool writes = false;
};

[[nodiscard]] std::span<const CliCommandDescriptor> CliCommandCatalog() noexcept;
[[nodiscard]] const CliCommandDescriptor* FindCliCommand(std::string_view name) noexcept;

// The usage text `kb_cli help` prints: the one description of every command. The machine-readable catalog
// (`kb_cli commands`) is read from it, so a command is described once, for people and for agents.
[[nodiscard]] std::string CliUsageText();
// One command's usage lines (empty when the text names no such command); the names of all described commands.
[[nodiscard]] std::string CliCommandUsageBlock(std::string_view command);
[[nodiscard]] std::vector<std::string> CliUsageCommandNames();
// The catalog as one JSON object: every command of the table with its purpose, subcommands and options
// (name, type, required, default), its --json output kind, availability and MCP tool name.
[[nodiscard]] std::string CliCatalogJson();

// The kb_cli program: `args` are the command line after the program name. Exits 0 on success and
// 1 on any failure. With --json, prints one result object instead of the command's text.
[[nodiscard]] int RunCli(std::span<const std::string> args, std::istream& in, CommandIo io);

// Runs one kb_cli command line (the words after the program name).
using CommandRunner = std::function<int(std::span<const std::string>, CommandIo)>;

// batch: runs the operations of a file as one transaction on a project; see the usage text.
[[nodiscard]] int RunBatchCommand(const ArgumentList& arguments, CommandIo io, const CommandRunner& run);

// The scenes that entity and component commands edit, kept open while a batch runs: the first command that
// names a scene opens it, the later ones edit the same scene in memory, and Commit writes it once. Any other
// command first has the scenes written and closed (Prepare), so it sees the files as they are. At most one exists.
class SceneEditBatch {
public:
    SceneEditBatch();
    ~SceneEditBatch();
    SceneEditBatch(const SceneEditBatch&) = delete;
    SceneEditBatch& operator=(const SceneEditBatch&) = delete;

    // Before the operation `command`; false, with the reason, when a scene could not be written.
    [[nodiscard]] bool Prepare(std::string_view command, std::string& error);
    // Writes the scenes that changed and closes them all; false, with the reason, when one could not be written.
    [[nodiscard]] bool Commit(std::string& error);
    // Closes the scenes without writing them.
    void Discard() noexcept;

private:
    struct State;
    std::unique_ptr<State> state_;
};

// Text as JSON can carry it: bytes that are not valid UTF-8 become U+FFFD.
[[nodiscard]] std::string ToValidUtf8(std::string_view text);

} // namespace kb::cli
