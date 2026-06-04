#pragma once

#include "engine/visual/VisualGraphTypes.hpp"

#include <istream>
#include <string>
#include <vector>

namespace kb::visual {

struct VisualGraphParseResult {
    VisualGraphAsset graph;
    std::vector<std::string> errors;

    [[nodiscard]] bool Succeeded() const noexcept {
        return errors.empty();
    }
};

class VisualGraphParser {
public:
    VisualGraphParser() = delete;

    [[nodiscard]] static VisualGraphParseResult Parse(std::istream& input);
};

} // namespace kb::visual
