#include "CliCommands.hpp"

#include "engine/assets/AssetId.hpp"
#include "engine/script/PucLuaScriptRuntime.hpp"

#include <fstream>
#include <sstream>

namespace kb::cli {

namespace {

[[nodiscard]] bool ReadTextFile(const std::filesystem::path& path, std::string& content, std::string& error) {
    std::ifstream input(path, std::ios::binary);
    if (!input.is_open()) {
        error = "could not open file: " + path.string();
        return false;
    }
    std::ostringstream buffer;
    buffer << input.rdbuf();
    content = buffer.str();
    return true;
}

} // namespace

int RunValidateCommand(const ArgumentList& arguments, CommandIo io) {
    if (arguments.Positionals().empty()) {
        io.err << "error: validate expects one or more .lua files\n";
        return 1;
    }

    const std::filesystem::path projectRoot = arguments.Option("--project").value_or("");
    kb::script::PucLuaScriptRuntime luaRuntime;
    std::uint64_t nextAssetId = 1U;
    int failures = 0;

    for (const std::string& positional : arguments.Positionals()) {
        const std::filesystem::path path = ResolveInputPath(positional, projectRoot);
        std::string source;
        std::string error;
        if (!ReadTextFile(path, source, error)) {
            io.err << "FAIL " << positional << ": " << error << '\n';
            ++failures;
            continue;
        }

        const kb::script::PucLuaLoadResult loaded = luaRuntime.LoadScript(
            kb::assets::AssetId{ nextAssetId++ },
            source,
            path.filename().string());
        if (!loaded.succeeded) {
            io.err << "FAIL " << positional << ": " << loaded.error << '\n';
            ++failures;
            continue;
        }
        io.out << "OK   " << positional << '\n';
    }

    return failures == 0 ? 0 : 1;
}

} // namespace kb::cli
