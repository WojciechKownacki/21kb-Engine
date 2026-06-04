#pragma once

#include "engine/visual/VisualGraphTypes.hpp"

#include <iosfwd>
#include <string>

namespace kb::visual {

class VisualGraphAssetWriter {
public:
    VisualGraphAssetWriter() = delete;

    [[nodiscard]] static std::string WriteToString(const VisualGraphAsset& graph);
    static void Write(std::ostream& output, const VisualGraphAsset& graph);
};

} // namespace kb::visual
