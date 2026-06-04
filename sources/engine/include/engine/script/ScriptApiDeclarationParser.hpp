#pragma once

#include "engine/script/ScriptApiNameRegistry.hpp"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace kb::script {

struct ScriptApiDeclarationParseResult {
    std::vector<ScriptApiNameEntry> entries;
    std::vector<std::string> errors;

    [[nodiscard]] bool Succeeded() const noexcept {
        return errors.empty();
    }
};

class ScriptApiDeclarationParser final {
public:
    ScriptApiDeclarationParser() = delete;

    [[nodiscard]] static std::optional<ScriptApiNameEntry> ParseDeclaration(
        std::string_view declaration,
        std::string owner = {},
        bool functionDeclaresProvider = false);
    [[nodiscard]] static ScriptApiDeclarationParseResult CollectMarkedDeclarations(
        std::string_view source,
        std::string owner = {},
        bool functionDeclaresProvider = false);
};

} // namespace kb::script
