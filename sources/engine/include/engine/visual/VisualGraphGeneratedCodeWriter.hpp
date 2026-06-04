#pragma once

#include "engine/visual/VisualGraphNativeCodeGenerator.hpp"

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace kb::visual {

struct VisualGraphGeneratedCodeFiles {
    std::filesystem::path headerPath;
    std::filesystem::path sourcePath;
};

struct VisualGraphGeneratedCodeWriteResult {
    VisualGraphGeneratedCodeFiles files;
    std::vector<std::string> errors;

    [[nodiscard]] bool Succeeded() const noexcept {
        return errors.empty();
    }
};

class VisualGraphGeneratedCodeWriter {
public:
    VisualGraphGeneratedCodeWriter() = delete;

    [[nodiscard]] static VisualGraphGeneratedCodeWriteResult Write(const VisualGraphNativeCode& code, const std::filesystem::path& outputDirectory);
};

} // namespace kb::visual
