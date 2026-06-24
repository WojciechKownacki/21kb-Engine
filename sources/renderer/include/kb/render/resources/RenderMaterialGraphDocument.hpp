#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace kb::render {

inline constexpr std::uint32_t kRenderMaterialGraphDocumentVersion = 1U;

enum class RenderMaterialGraphNodeKind : std::uint8_t {
    MaterialOutput,
    ConstantScalar,
    ConstantColor,
    TextureSample,
    ParameterScalar,
    ParameterColor,
    ParameterTexture,
};

struct RenderMaterialGraphNode {
    std::uint32_t id = 0U;
    RenderMaterialGraphNodeKind kind = RenderMaterialGraphNodeKind::MaterialOutput;
    std::int32_t positionX = 0;
    std::int32_t positionY = 0;
};

struct RenderMaterialGraphLink {
    std::uint32_t fromNodeId = 0U;
    std::string fromPin;
    std::uint32_t toNodeId = 0U;
    std::string toPin;
};

struct RenderMaterialGraphDocument {
    std::uint32_t documentVersion = kRenderMaterialGraphDocumentVersion;
    bool hasExplicitDocumentVersion = false;
    std::vector<RenderMaterialGraphNode> nodes;
    std::vector<RenderMaterialGraphLink> links;
};

[[nodiscard]] std::string_view RenderMaterialGraphNodeKindName(RenderMaterialGraphNodeKind kind) noexcept;
[[nodiscard]] std::optional<RenderMaterialGraphNodeKind> ParseRenderMaterialGraphNodeKind(std::string_view text) noexcept;
[[nodiscard]] RenderMaterialGraphDocument MakeDefaultRenderMaterialGraphDocument();
[[nodiscard]] const RenderMaterialGraphNode* FindRenderMaterialGraphNode(const RenderMaterialGraphDocument& graph, std::uint32_t nodeId) noexcept;
[[nodiscard]] bool IsRenderMaterialGraphInputPin(RenderMaterialGraphNodeKind kind, std::string_view pin) noexcept;
[[nodiscard]] bool IsRenderMaterialGraphOutputPin(RenderMaterialGraphNodeKind kind, std::string_view pin) noexcept;

} // namespace kb::render
