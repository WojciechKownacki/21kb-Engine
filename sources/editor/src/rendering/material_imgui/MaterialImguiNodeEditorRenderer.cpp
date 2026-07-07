#include "rendering/material_imgui/MaterialImguiNodeEditorRenderer.hpp"

#include <imgui.h>
#include <imgui_node_editor.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <ranges>

namespace kb::editor {
namespace {

namespace ed = ax::NodeEditor;

[[nodiscard]] ImVec4 WithAlpha(ImVec4 color, float alpha) noexcept {
    color.w = alpha;
    return color;
}

void PushMaterialNodeEditorStyle() {
    ed::PushStyleColor(ed::StyleColor_Bg, ImVec4(0.018F, 0.022F, 0.030F, 1.0F));
    ed::PushStyleColor(ed::StyleColor_Grid, ImVec4(0.19F, 0.25F, 0.32F, 0.28F));
    ed::PushStyleColor(ed::StyleColor_NodeBg, ImVec4(0.040F, 0.050F, 0.064F, 0.99F));
    ed::PushStyleColor(ed::StyleColor_NodeBorder, ImVec4(0.18F, 0.24F, 0.31F, 1.0F));
    ed::PushStyleColor(ed::StyleColor_HovNodeBorder, ImVec4(0.30F, 0.87F, 0.82F, 1.0F));
    ed::PushStyleColor(ed::StyleColor_SelNodeBorder, ImVec4(0.20F, 0.95F, 0.88F, 1.0F));
    ed::PushStyleColor(ed::StyleColor_HovLinkBorder, ImVec4(0.95F, 0.75F, 0.25F, 1.0F));
    ed::PushStyleColor(ed::StyleColor_SelLinkBorder, ImVec4(1.0F, 0.82F, 0.30F, 1.0F));
    ed::PushStyleColor(ed::StyleColor_PinRect, ImVec4(0.95F, 0.95F, 0.95F, 1.0F));
    ed::PushStyleColor(ed::StyleColor_PinRectBorder, ImVec4(0.02F, 0.025F, 0.03F, 1.0F));

    ed::PushStyleVar(ed::StyleVar_NodePadding, ImVec4(0.0F, 0.0F, 0.0F, 10.0F));
    ed::PushStyleVar(ed::StyleVar_NodeRounding, 5.0F);
    ed::PushStyleVar(ed::StyleVar_NodeBorderWidth, 1.25F);
    ed::PushStyleVar(ed::StyleVar_SelectedNodeBorderWidth, 2.0F);
    ed::PushStyleVar(ed::StyleVar_PinRadius, 5.5F);
    ed::PushStyleVar(ed::StyleVar_LinkStrength, 82.0F);
    ed::PushStyleVar(ed::StyleVar_SourceDirection, ImVec2(1.0F, 0.0F));
    ed::PushStyleVar(ed::StyleVar_TargetDirection, ImVec2(-1.0F, 0.0F));
}

void PopMaterialNodeEditorStyle() {
    ed::PopStyleVar(8);
    ed::PopStyleColor(10);
}

void DrawSocketCircle(const MaterialImguiPin& pin, const ImVec2& center) {
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    drawList->AddCircleFilled(center, 4.6F, ImGui::ColorConvertFloat4ToU32(pin.color), 18);
    drawList->AddCircle(center, 5.7F, ImGui::ColorConvertFloat4ToU32(ImVec4(0.015F, 0.018F, 0.024F, 1.0F)), 18, 1.0F);
}

void DrawSocketPinItem(const MaterialImguiPin& pin, ed::PinKind kind, const ImVec2& center) {
    constexpr float hitSize = 16.0F;
    ImGui::PushID(static_cast<int>(pin.stablePinId));
    ImGui::SetCursorScreenPos(ImVec2(center.x - hitSize * 0.5F, center.y - hitSize * 0.5F));
    ed::BeginPin(pin.editorId, kind);
    ImGui::InvisibleButton("##socket", ImVec2(hitSize, hitSize));
    ed::EndPin();
    ImGui::PopID();
    DrawSocketCircle(pin, center);
}

void DrawPinRow(const MaterialImguiPin* input, const MaterialImguiPin* output, float rowWidth) {
    constexpr float rowHeight = 20.0F;
    constexpr float socketInset = 6.0F;
    const ImVec2 rowMin = ImGui::GetCursorScreenPos();
    const ImVec2 inputCenter{ rowMin.x + socketInset, rowMin.y + rowHeight * 0.5F };
    const ImVec2 outputCenter{ rowMin.x + rowWidth - socketInset, rowMin.y + rowHeight * 0.5F };
    ImDrawList* drawList = ImGui::GetWindowDrawList();

    if (input != nullptr) {
        DrawSocketPinItem(*input, ed::PinKind::Input, inputCenter);
        drawList->AddText(
            ImVec2(rowMin.x + 18.0F, rowMin.y + 2.0F),
            ImGui::ColorConvertFloat4ToU32(ImVec4(0.86F, 0.91F, 0.97F, 1.0F)),
            input->label.c_str());
    }

    if (output != nullptr) {
        const ImVec2 textSize = ImGui::CalcTextSize(output->label.c_str());
        drawList->AddText(
            ImVec2(std::max(rowMin.x + 64.0F, outputCenter.x - 12.0F - textSize.x), rowMin.y + 2.0F),
            ImGui::ColorConvertFloat4ToU32(ImVec4(0.86F, 0.91F, 0.97F, 1.0F)),
            output->label.c_str());
        DrawSocketPinItem(*output, ed::PinKind::Output, outputCenter);
    }

    ImGui::SetCursorScreenPos(rowMin);
    ImGui::Dummy(ImVec2(rowWidth, rowHeight));
}

void DrawNodeHeader(const MaterialImguiNode& node, float width) {
    const ImVec2 min = ImGui::GetCursorScreenPos();
    const ImVec2 max{ min.x + width, min.y + 28.0F };
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    drawList->AddRectFilledMultiColor(
        min,
        max,
        ImGui::ColorConvertFloat4ToU32(WithAlpha(node.headerColor, 0.98F)),
        ImGui::ColorConvertFloat4ToU32(WithAlpha(node.headerColor, 0.90F)),
        ImGui::ColorConvertFloat4ToU32(WithAlpha(ImVec4(node.headerColor.x * 0.50F, node.headerColor.y * 0.50F, node.headerColor.z * 0.55F, 1.0F), 0.96F)),
        ImGui::ColorConvertFloat4ToU32(WithAlpha(ImVec4(node.headerColor.x * 0.56F, node.headerColor.y * 0.56F, node.headerColor.z * 0.60F, 1.0F), 0.96F)));
    drawList->AddLine(
        ImVec2(min.x, max.y - 1.0F),
        ImVec2(max.x, max.y - 1.0F),
        ImGui::ColorConvertFloat4ToU32(ImVec4(0.04F, 0.05F, 0.065F, 0.90F)),
        1.0F);
    const ImVec2 textSize = ImGui::CalcTextSize(node.title.c_str());
    drawList->AddText(
        ImVec2(min.x + std::floor((width - textSize.x) * 0.5F), min.y + std::floor((28.0F - textSize.y) * 0.5F)),
        ImGui::ColorConvertFloat4ToU32(ImVec4(0.94F, 0.98F, 1.0F, 1.0F)),
        node.title.c_str());
    ImGui::Dummy(ImVec2(width, 28.0F));
}

[[nodiscard]] bool HasTexturePreviewBody(const MaterialImguiNode& node) noexcept {
    return node.bodyKind == MaterialImguiNodeBodyKind::TextureSamplePreview ||
        node.bodyKind == MaterialImguiNodeBodyKind::TextureObjectPreview;
}

void DrawCheckerboard(ImDrawList& drawList, const ImVec2& min, const ImVec2& max, float cellSize) {
    const ImU32 dark = ImGui::ColorConvertFloat4ToU32(ImVec4(0.055F, 0.067F, 0.083F, 1.0F));
    const ImU32 light = ImGui::ColorConvertFloat4ToU32(ImVec4(0.088F, 0.105F, 0.128F, 1.0F));
    for (float y = min.y; y < max.y; y += cellSize) {
        for (float x = min.x; x < max.x; x += cellSize) {
            const int xi = static_cast<int>((x - min.x) / cellSize);
            const int yi = static_cast<int>((y - min.y) / cellSize);
            const ImU32 color = ((xi + yi) % 2) == 0 ? dark : light;
            drawList.AddRectFilled(
                ImVec2(x, y),
                ImVec2(std::min(x + cellSize, max.x), std::min(y + cellSize, max.y)),
                color);
        }
    }
}

void DrawTexturePreviewSlot(const MaterialImguiNode& node, const MaterialImguiTextureResolver& textureResolver) {
    constexpr ImVec2 previewSize{ 220.0F, 128.0F };
    const ImVec2 min = ImGui::GetCursorScreenPos();
    const ImVec2 max{ min.x + previewSize.x, min.y + previewSize.y };
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    drawList->AddRectFilled(min, max, ImGui::ColorConvertFloat4ToU32(ImVec4(0.025F, 0.031F, 0.040F, 1.0F)), 4.0F);
    const ImVec2 imageMin{ min.x + 1.0F, min.y + 1.0F };
    const ImVec2 imageMax{ max.x - 1.0F, max.y - 1.0F };
    DrawCheckerboard(*drawList, imageMin, imageMax, 12.0F);
    ImTextureID textureId = nullptr;
    if (node.texturePreview.has_value() && node.texturePreview->IsValid() && textureResolver) {
        textureId = textureResolver(*node.texturePreview);
    }
    if (textureId != nullptr && node.texturePreview.has_value()) {
        const float sourceWidth = static_cast<float>(node.texturePreview->width);
        const float sourceHeight = static_cast<float>(node.texturePreview->height);
        const float scale = std::min((imageMax.x - imageMin.x) / sourceWidth, (imageMax.y - imageMin.y) / sourceHeight);
        const ImVec2 imageSize{ std::max(1.0F, std::floor(sourceWidth * scale)), std::max(1.0F, std::floor(sourceHeight * scale)) };
        const ImVec2 drawMin{
            imageMin.x + std::floor(((imageMax.x - imageMin.x) - imageSize.x) * 0.5F),
            imageMin.y + std::floor(((imageMax.y - imageMin.y) - imageSize.y) * 0.5F),
        };
        const ImVec2 drawMax{ drawMin.x + imageSize.x, drawMin.y + imageSize.y };
        drawList->AddImage(textureId, drawMin, drawMax);
    } else {
        drawList->AddRectFilledMultiColor(
            imageMin,
            imageMax,
            ImGui::ColorConvertFloat4ToU32(ImVec4(0.12F, 0.19F, 0.24F, 0.28F)),
            ImGui::ColorConvertFloat4ToU32(ImVec4(0.04F, 0.07F, 0.09F, 0.20F)),
            ImGui::ColorConvertFloat4ToU32(ImVec4(0.02F, 0.03F, 0.04F, 0.32F)),
            ImGui::ColorConvertFloat4ToU32(ImVec4(0.06F, 0.10F, 0.12F, 0.28F)));
    }
    drawList->AddRect(min, max, ImGui::ColorConvertFloat4ToU32(ImVec4(0.26F, 0.45F, 0.49F, 0.90F)), 4.0F, 0, 1.0F);
    ImGui::Dummy(previewSize);
}

void DrawMaterialNode(const MaterialImguiNode& node, const MaterialImguiTextureResolver& textureResolver) {
    ed::BeginNode(node.editorId);
    ImGui::PushID(static_cast<int>(node.nodeId));
    constexpr float nodeWidth = 300.0F;
    constexpr float bodyWidth = nodeWidth - 24.0F;
    DrawNodeHeader(node, nodeWidth);
    ImGui::Dummy(ImVec2(1.0F, 8.0F));
    ImGui::Indent(12.0F);

    if (HasTexturePreviewBody(node)) {
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::floor((bodyWidth - 220.0F) * 0.5F));
        DrawTexturePreviewSlot(node, textureResolver);
        ImGui::Dummy(ImVec2(1.0F, 7.0F));
    }

    const std::size_t rows = std::max(node.inputs.size(), node.outputs.size());
    for (std::size_t row = 0U; row < rows; ++row) {
        const MaterialImguiPin* input = row < node.inputs.size() ? &node.inputs[row] : nullptr;
        const MaterialImguiPin* output = row < node.outputs.size() ? &node.outputs[row] : nullptr;
        DrawPinRow(input, output, bodyWidth);
    }

    ImGui::Unindent(12.0F);
    ImGui::PopID();
    ed::EndNode();
}

void SubmitLinks(const MaterialImguiNodeEditorModel& model) {
    for (const MaterialImguiLink& link : model.links) {
        ed::Link(link.editorId, link.startPin, link.endPin, link.color, 2.6F);
    }
}

[[nodiscard]] MaterialImguiNodeEditorFrameResult GatherInteractions() {
    MaterialImguiNodeEditorFrameResult result{};

    if (ed::BeginCreate(ImVec4(0.95F, 0.75F, 0.25F, 1.0F), 2.5F)) {
        ed::PinId startPin{};
        ed::PinId endPin{};
        if (ed::QueryNewLink(&startPin, &endPin) && startPin && endPin && ed::AcceptNewItem()) {
            result.acceptedNewLink = MaterialImguiNodeEditorNewLink{ .startPin = startPin, .endPin = endPin };
        }
    }
    ed::EndCreate();

    if (ed::BeginDelete()) {
        ed::LinkId link{};
        ed::PinId startPin{};
        ed::PinId endPin{};
        while (ed::QueryDeletedLink(&link, &startPin, &endPin)) {
            if (ed::AcceptDeletedItem()) {
                result.acceptedDeletedLinks.push_back(MaterialImguiNodeEditorDeletedLink{
                    .link = link,
                    .startPin = startPin,
                    .endPin = endPin,
                });
            }
        }
    }
    ed::EndDelete();

    return result;
}

[[nodiscard]] bool SamePosition(
    const MaterialImguiNodeEditorMovedNode& position,
    const MaterialImguiNode& node) noexcept {
    return position.nodeId == node.nodeId && position.positionX == node.positionX && position.positionY == node.positionY;
}

[[nodiscard]] MaterialImguiNodeEditorMovedNode ModelPositionFor(const MaterialImguiNode& node) noexcept {
    return MaterialImguiNodeEditorMovedNode{
        .nodeId = node.nodeId,
        .positionX = node.positionX,
        .positionY = node.positionY,
    };
}

} // namespace

std::optional<MaterialImguiNodeEditorGraphLinkRequest> ResolveMaterialImguiNewLink(
    const MaterialImguiNodeEditorModel& model,
    const MaterialImguiNodeEditorNewLink& link) noexcept {
    const MaterialImguiPin* startPin = model.FindPin(link.startPin);
    const MaterialImguiPin* endPin = model.FindPin(link.endPin);
    if (startPin == nullptr || endPin == nullptr || startPin->direction == endPin->direction) {
        return std::nullopt;
    }

    const MaterialImguiPin* output = startPin;
    const MaterialImguiPin* input = endPin;
    if (startPin->direction == MaterialImguiPinDirection::Input) {
        output = endPin;
        input = startPin;
    }
    return MaterialImguiNodeEditorGraphLinkRequest{
        .fromNodeId = output->nodeId,
        .fromPin = output->name,
        .toNodeId = input->nodeId,
        .toPin = input->name,
    };
}

std::optional<MaterialImguiNodeEditorGraphLinkRequest> ResolveMaterialImguiDeletedLink(
    const MaterialImguiNodeEditorModel& model,
    const MaterialImguiNodeEditorDeletedLink& link) noexcept {
    if (const MaterialImguiLink* graphLink = model.FindLink(link.link); graphLink != nullptr) {
        return MaterialImguiNodeEditorGraphLinkRequest{
            .fromNodeId = graphLink->fromNodeId,
            .fromPin = graphLink->fromPin,
            .toNodeId = graphLink->toNodeId,
            .toPin = graphLink->toPin,
        };
    }
    return ResolveMaterialImguiNewLink(
        model,
        MaterialImguiNodeEditorNewLink{
            .startPin = link.startPin,
            .endPin = link.endPin,
        });
}

void MaterialImguiNodeEditorRenderer::EditorContextDeleter::operator()(ed::EditorContext* context) const noexcept {
    ed::DestroyEditor(context);
}

MaterialImguiNodeEditorRenderer::MaterialImguiNodeEditorRenderer()
    : context_(ed::CreateEditor()) {
}

MaterialImguiNodeEditorRenderer::~MaterialImguiNodeEditorRenderer() = default;

MaterialImguiNodeEditorRenderer::MaterialImguiNodeEditorRenderer(MaterialImguiNodeEditorRenderer&&) noexcept = default;

MaterialImguiNodeEditorRenderer& MaterialImguiNodeEditorRenderer::operator=(MaterialImguiNodeEditorRenderer&&) noexcept = default;

MaterialImguiNodeEditorFrameResult MaterialImguiNodeEditorRenderer::Render(
    const MaterialImguiNodeEditorModel& model,
    const ImVec2& size,
    const MaterialImguiTextureResolver& textureResolver) {
    ed::SetCurrentEditor(context_.get());
    PushMaterialNodeEditorStyle();
    ed::Begin("MaterialGraphImguiNodeEditor", size);

    for (const MaterialImguiNode& node : model.nodes) {
        const auto applied = std::ranges::find_if(lastAppliedModelPositions_, [&node](const MaterialImguiNodeEditorMovedNode& position) {
            return position.nodeId == node.nodeId;
        });
        const bool modelPositionChanged = applied == lastAppliedModelPositions_.end() || !SamePosition(*applied, node);
        if (modelPositionChanged) {
            ed::SetNodePosition(node.editorId, ImVec2(static_cast<float>(node.positionX), static_cast<float>(node.positionY)));
            if (applied == lastAppliedModelPositions_.end()) {
                lastAppliedModelPositions_.push_back(ModelPositionFor(node));
            } else {
                *applied = ModelPositionFor(node);
            }
        }
        DrawMaterialNode(node, textureResolver);
    }
    SubmitLinks(model);
    MaterialImguiNodeEditorFrameResult result = GatherInteractions();
    if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
        for (const MaterialImguiNode& node : model.nodes) {
            const ImVec2 position = ed::GetNodePosition(node.editorId);
            const std::int32_t positionX = static_cast<std::int32_t>(std::lround(position.x));
            const std::int32_t positionY = static_cast<std::int32_t>(std::lround(position.y));
            if (positionX != node.positionX || positionY != node.positionY) {
                result.movedNodes.push_back(MaterialImguiNodeEditorMovedNode{
                    .nodeId = node.nodeId,
                    .positionX = positionX,
                    .positionY = positionY,
                });
            }
        }
    }

    ed::End();
    PopMaterialNodeEditorStyle();
    ed::SetCurrentEditor(nullptr);
    return result;
}

} // namespace kb::editor
