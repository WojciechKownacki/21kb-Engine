#include "engine/visual/VisualGraphGeneratedCodeWriter.hpp"

#include <fstream>

namespace kb::visual {
namespace {

[[nodiscard]] bool WriteTextFile(const std::filesystem::path& path, std::string_view text) {
    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);
    if (error) {
        return false;
    }

    std::ofstream output{path, std::ios::binary | std::ios::trunc};
    if (!output.is_open()) {
        return false;
    }

    output << text;
    return output.good();
}

} // namespace

VisualGraphGeneratedCodeWriteResult VisualGraphGeneratedCodeWriter::Write(const VisualGraphNativeCode& code, const std::filesystem::path& outputDirectory) {
    VisualGraphGeneratedCodeWriteResult result{};
    if (!code.Succeeded()) {
        result.errors = code.errors;
        return result;
    }
    if (code.headerFileName.empty() || code.sourceFileName.empty()) {
        result.errors.push_back("visual graph native code has no output file names");
        return result;
    }
    if (outputDirectory.empty()) {
        result.errors.push_back("visual graph generated code output directory is empty");
        return result;
    }

    result.files.headerPath = outputDirectory / code.headerFileName;
    result.files.sourcePath = outputDirectory / code.sourceFileName;
    if (!WriteTextFile(result.files.headerPath, code.header)) {
        result.errors.push_back("visual graph generated header could not be written");
    }
    if (!WriteTextFile(result.files.sourcePath, code.source)) {
        result.errors.push_back("visual graph generated source could not be written");
    }
    return result;
}

} // namespace kb::visual
