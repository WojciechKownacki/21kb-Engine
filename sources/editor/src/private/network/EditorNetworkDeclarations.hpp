#pragma once

#include "engine/network/ReplicationDeclarations.hpp"

#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

namespace kb::editor {

// The project's replication declarations as the editor edits them. Config/Replication.json is the only
// place the rows live: the Inspector's Networked Variable section and the project's Network page read
// and write this file, and the engine parses the same text at runtime.
class EditorNetworkDeclarations final {
public:
    // <project root>/Config/Replication.json
    [[nodiscard]] static std::filesystem::path FilePath(const std::filesystem::path& projectRoot);

    // Reads the file. A missing file is empty declarations and not an error; a file the engine refuses
    // reports its error and leaves the declarations empty.
    [[nodiscard]] bool Load(const std::filesystem::path& projectRoot, std::string& error);

    [[nodiscard]] const kb::network::ReplicationDeclarations& Declarations() const noexcept { return declarations_; }
    [[nodiscard]] const std::vector<kb::network::ReplicatedVariableDecl>& Variables() const noexcept {
        return declarations_.variables;
    }

    // Adds a row after the last one, changes the row at `index`, or takes it out. Each change is written
    // to the file once the engine accepts it; a refused row leaves the file as it was.
    [[nodiscard]] bool AddVariable(const kb::network::ReplicatedVariableDecl& variable, std::string& error);
    [[nodiscard]] bool SetVariable(std::size_t index, const kb::network::ReplicatedVariableDecl& variable,
        std::string& error);
    [[nodiscard]] bool RemoveVariable(std::size_t index, std::string& error);

private:
    // Stores `next` and rewrites the file when the engine parses the text it writes.
    [[nodiscard]] bool Commit(kb::network::ReplicationDeclarations next, std::string& error);

    std::filesystem::path file_;
    kb::network::ReplicationDeclarations declarations_;
};

} // namespace kb::editor
