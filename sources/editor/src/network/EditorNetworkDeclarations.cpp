#include "network/EditorNetworkDeclarations.hpp"

#include <fstream>
#include <iterator>
#include <system_error>

namespace kb::editor {

namespace {
using kb::network::ReplicatedVariableDecl;

// A row the panel adds: the range and precision its type supports. A Float keeps 16 bits of fraction, which
// is what a slider over a bounded range needs; an Int and a Bool take the precision the declarations imply.
[[nodiscard]] ReplicatedVariableDecl Normalized(const ReplicatedVariableDecl& variable) {
    ReplicatedVariableDecl row = variable;
    if (row.type == ReplicatedVariableDecl::Type::Bool) {
        row.minimum = 0.0F;
        row.maximum = 1.0F;
        row.bits = 1U;
    } else if (row.type == ReplicatedVariableDecl::Type::Int) {
        row.bits = 0U;
    } else if (row.bits == 0U) {
        row.bits = 16U;
    }
    return row;
}
} // namespace

std::filesystem::path EditorNetworkDeclarations::FilePath(const std::filesystem::path& projectRoot) {
    return projectRoot / "Config" / "Replication.json";
}

bool EditorNetworkDeclarations::Load(const std::filesystem::path& projectRoot, std::string& error) {
    file_ = FilePath(projectRoot);
    declarations_ = {};
    error.clear();
    std::error_code fileError;
    if (!std::filesystem::is_regular_file(file_, fileError)) {
        if (fileError) {
            error = "replication declarations could not be checked: " + fileError.message();
            return false;
        }
        return true;
    }
    std::ifstream file(file_, std::ios::binary);
    if (!file) {
        error = "replication declarations could not be opened";
        return false;
    }
    const std::string text{ std::istreambuf_iterator<char>{ file }, std::istreambuf_iterator<char>{} };
    if (!kb::network::ParseReplicationDeclarations(text, declarations_, error)) {
        declarations_ = {};
        return false;
    }
    return true;
}

bool EditorNetworkDeclarations::AddVariable(const ReplicatedVariableDecl& variable, std::string& error) {
    kb::network::ReplicationDeclarations next = declarations_;
    next.variables.push_back(Normalized(variable));
    return Commit(std::move(next), error);
}

bool EditorNetworkDeclarations::SetVariable(std::size_t index, const ReplicatedVariableDecl& variable,
    std::string& error) {
    if (index >= declarations_.variables.size()) {
        error = "no replication variable at that row";
        return false;
    }
    kb::network::ReplicationDeclarations next = declarations_;
    next.variables[index] = Normalized(variable);
    return Commit(std::move(next), error);
}

bool EditorNetworkDeclarations::RemoveVariable(std::size_t index, std::string& error) {
    if (index >= declarations_.variables.size()) {
        error = "no replication variable at that row";
        return false;
    }
    kb::network::ReplicationDeclarations next = declarations_;
    next.variables.erase(next.variables.begin() + static_cast<std::ptrdiff_t>(index));
    return Commit(std::move(next), error);
}

bool EditorNetworkDeclarations::Commit(kb::network::ReplicationDeclarations next, std::string& error) {
    if (file_.empty()) {
        error = "the project has no replication declarations path";
        return false;
    }
    const std::string text = kb::network::WriteReplicationDeclarations(next);
    // The file is the only source, so it is only written when the engine reads it back unchanged.
    kb::network::ReplicationDeclarations checked;
    if (!kb::network::ParseReplicationDeclarations(text, checked, error)) return false;
    std::error_code directoryError;
    std::filesystem::create_directories(file_.parent_path(), directoryError);
    if (directoryError) {
        error = "replication declarations directory could not be created: " + directoryError.message();
        return false;
    }
    std::ofstream file(file_, std::ios::binary | std::ios::trunc);
    if (!file) {
        error = "replication declarations could not be written";
        return false;
    }
    file.write(text.data(), static_cast<std::streamsize>(text.size()));
    if (!file.good()) {
        error = "replication declarations could not be written";
        return false;
    }
    declarations_ = std::move(checked);
    return true;
}

} // namespace kb::editor
