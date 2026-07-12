#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace kb::editor {

struct MaterialEditorMaterialStatsPassRow {
    std::string passName;
    bool graphProgram = false;
    bool cacheHit = false;
    bool instructionCountAvailable = false;
    std::uint32_t instructionCount = 0U;
    std::uint32_t samplerCount = 0U;
    std::uint32_t uniformCount = 0U;
    std::uint32_t varyingCount = 0U;
    std::uint32_t staticVariantCount = 1U;
    std::uint64_t binaryByteSize = 0U;
    std::string backendName;
    std::vector<std::string> warnings;
};

struct MaterialEditorCookPassTelemetry {
    std::string passName;
    bool succeeded = false;
    bool cacheHit = false;
    std::uint64_t binaryByteSize = 0U;
};

struct MaterialEditorMaterialStatsModel {
    bool available = false;
    std::uint64_t sourceHash = 0U;
    std::vector<MaterialEditorMaterialStatsPassRow> passRows;
    std::vector<std::string> warnings;
};

struct MaterialEditorShaderSourceView {
    std::string passName;
    std::string backendName;
    std::string stageName;
    std::string source;
    std::uint64_t sourceHash = 0U;
};

struct MaterialEditorShaderReflectionRow {
    std::string category;
    std::string name;
    std::string stableId;
    std::string detail;
};

struct MaterialEditorShaderViewerModel {
    bool available = false;
    std::uint64_t sourceHash = 0U;
    std::uint64_t reflectionHash = 0U;
    std::vector<MaterialEditorShaderSourceView> sources;
    std::vector<MaterialEditorShaderReflectionRow> reflectionRows;
    std::vector<std::string> warnings;
};

enum class MaterialEditorFindResultKind : std::uint8_t {
    Node,
    Pin,
    Parameter,
    Comment,
};

struct MaterialEditorFindResult {
    MaterialEditorFindResultKind kind = MaterialEditorFindResultKind::Node;
    std::uint32_t nodeId = 0U;
    std::uint32_t commentId = 0U;
    std::string label;
    std::string detail;
    std::int32_t focusX = 0;
    std::int32_t focusY = 0;
};

struct MaterialEditorFindFocusTarget {
    std::int32_t graphX = 0;
    std::int32_t graphY = 0;
};

} // namespace kb::editor
