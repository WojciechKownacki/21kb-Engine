#include "rendering/script_editor/ScriptSourceFile.hpp"

#include <fstream>
#include <iterator>

namespace kb::editor {

std::string ScriptSourceFile::Read(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        return {};
    }
    return std::string{ std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>() };
}

bool ScriptSourceFile::Write(const std::filesystem::path& path, std::string_view text) {
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    if (!stream) {
        return false;
    }
    stream.write(text.data(), static_cast<std::streamsize>(text.size()));
    return stream.good();
}

} // namespace kb::editor
