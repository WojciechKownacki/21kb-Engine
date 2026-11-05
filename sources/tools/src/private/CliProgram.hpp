#pragma once

#include "CliCommon.hpp"

#include <functional>
#include <istream>
#include <memory>
#include <span>
#include <string>
#include <string_view>

namespace kb::cli {

enum class CliCommandKind { Normal, Particle, Batch, Mcp, Help, Catalog };

struct CliCommandDescriptor {
    std::string_view name;
    int (*run)(const ArgumentList&, CommandIo) = nullptr;
    CliCommandKind kind = CliCommandKind::Normal;
    bool available = true;
    std::string_view mcpTool;
};

[[nodiscard]] std::span<const CliCommandDescriptor> CliCommandCatalog() noexcept;
[[nodiscard]] const CliCommandDescriptor* FindCliCommand(std::string_view name) noexcept;

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
