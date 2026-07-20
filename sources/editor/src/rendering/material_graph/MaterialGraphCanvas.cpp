#include "rendering/material_graph/MaterialGraphCanvas.hpp"
#include "rendering/material_graph/MaterialGraphInteractionPolicy.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>
#include <string>
#include <utility>

namespace kb::editor {
namespace {

[[nodiscard]] bool ValidIndex(std::uint32_t index, std::size_t size) noexcept {
    return index < size;
}

[[nodiscard]] float DistancePointSegment(
    MaterialGraphCanvasPoint point,
    MaterialGraphCanvasPoint a,
    MaterialGraphCanvasPoint b) noexcept {
    const float vx = b.x - a.x;
    const float vy = b.y - a.y;
    const float len2 = (vx * vx) + (vy * vy);
    float t = (len2 > 0.0F) ? (((point.x - a.x) * vx) + ((point.y - a.y) * vy)) / len2 : 0.0F;
    t = std::clamp(t, 0.0F, 1.0F);
    return std::hypot(point.x - (a.x + (t * vx)), point.y - (a.y + (t * vy)));
}

[[nodiscard]] MaterialGraphCanvasPoint CubicBezier(
    MaterialGraphCanvasPoint p0,
    MaterialGraphCanvasPoint p1,
    MaterialGraphCanvasPoint p2,
    MaterialGraphCanvasPoint p3,
    float t) noexcept {
    const float u = 1.0F - t;
    const float w0 = u * u * u;
    const float w1 = 3.0F * u * u * t;
    const float w2 = 3.0F * u * t * t;
    const float w3 = t * t * t;
    return MaterialGraphCanvasPoint{
        (w0 * p0.x) + (w1 * p1.x) + (w2 * p2.x) + (w3 * p3.x),
        (w0 * p0.y) + (w1 * p1.y) + (w2 * p2.y) + (w3 * p3.y),
    };
}

[[nodiscard]] float DistancePointBezier(
    MaterialGraphCanvasPoint point,
    MaterialGraphCanvasPoint p0,
    MaterialGraphCanvasPoint p1,
    MaterialGraphCanvasPoint p2,
    MaterialGraphCanvasPoint p3) noexcept {
    constexpr int kSteps = 24;
    MaterialGraphCanvasPoint previous = p0;
    float best = std::hypot(point.x - p0.x, point.y - p0.y);
    for (int step = 1; step <= kSteps; ++step) {
        const MaterialGraphCanvasPoint current = CubicBezier(
            p0,
            p1,
            p2,
            p3,
            static_cast<float>(step) / static_cast<float>(kSteps));
        best = std::min(best, DistancePointSegment(point, previous, current));
        previous = current;
    }
    return best;
}

} // namespace

std::uint32_t MaterialGraphCanvas::AddNode(MaterialGraphCanvasNode node) {
    nodes_.push_back(std::move(node));
    return static_cast<std::uint32_t>(nodes_.size() - 1U);
}

void MaterialGraphCanvas::AddLink(MaterialGraphCanvasLink link) {
    if (!ValidIndex(link.fromNode, nodes_.size()) || !ValidIndex(link.toNode, nodes_.size())) {
        return;
    }
    if (!ValidIndex(link.fromPin, nodes_[link.fromNode].outputs.size()) ||
        !ValidIndex(link.toPin, nodes_[link.toNode].inputs.size())) {
        return;
    }
    links_.push_back(std::move(link));
}

void MaterialGraphCanvas::AddOccluderWorld(MaterialGraphCanvasRect rect) {
    if (rect.width > 0.0F && rect.height > 0.0F) {
        occluders_.push_back(rect);
    }
}

void MaterialGraphCanvas::Clear() noexcept {
    nodes_.clear();
    links_.clear();
    occluders_.clear();
}

void MaterialGraphCanvas::SetViewport(MaterialGraphCanvasRect viewport) noexcept {
    viewport_ = viewport;
}

void MaterialGraphCanvas::SetView(float panX, float panY, float zoom) noexcept {
    panX_ = panX;
    panY_ = panY;
    zoom_ = std::clamp(
        zoom,
        MaterialGraphInteractionPolicy::MinimumZoom,
        MaterialGraphInteractionPolicy::MaximumZoom);
}

MaterialGraphCanvasRect MaterialGraphCanvas::Viewport() const noexcept {
    return viewport_;
}

float MaterialGraphCanvas::PanX() const noexcept {
    return panX_;
}

float MaterialGraphCanvas::PanY() const noexcept {
    return panY_;
}

float MaterialGraphCanvas::Zoom() const noexcept {
    return zoom_;
}

std::size_t MaterialGraphCanvas::NodeCount() const noexcept {
    return nodes_.size();
}

std::size_t MaterialGraphCanvas::LinkCount() const noexcept {
    return links_.size();
}

const std::vector<MaterialGraphCanvasNode>& MaterialGraphCanvas::Nodes() const noexcept {
    return nodes_;
}

const std::vector<MaterialGraphCanvasLink>& MaterialGraphCanvas::Links() const noexcept {
    return links_;
}

const MaterialGraphCanvasNode* MaterialGraphCanvas::NodeAt(std::uint32_t node) const noexcept {
    return ValidIndex(node, nodes_.size()) ? &nodes_[node] : nullptr;
}

MaterialGraphCanvasNode* MaterialGraphCanvas::MutableNodeAt(std::uint32_t node) noexcept {
    return ValidIndex(node, nodes_.size()) ? &nodes_[node] : nullptr;
}

float MaterialGraphCanvas::TotalValueFieldRows(const MaterialGraphCanvasNode& node) const noexcept {
    float rows = 0.0F;
    for (const MaterialGraphCanvasValueField& field : node.valueFields) {
        rows += static_cast<float>(std::max(1, field.rowSpan));
    }
    return rows;
}

float MaterialGraphCanvas::NodeWidth(const MaterialGraphCanvasNode& node) const noexcept {
    return node.widthOverride > 0.0F ? node.widthOverride : DefaultNodeWidth;
}

float MaterialGraphCanvas::NodeHeight(const MaterialGraphCanvasNode& node) const noexcept {
    if (node.heightOverride > 0.0F) {
        return node.heightOverride;
    }
    std::size_t rows = std::max(node.inputs.size(), node.outputs.size());
    std::size_t fieldRows = 0U;
    for (const MaterialGraphCanvasValueField& field : node.valueFields) {
        fieldRows += static_cast<std::size_t>(std::max(1, field.rowSpan));
    }
    rows = std::max(rows, node.inputs.size() + fieldRows);
    rows = std::max<std::size_t>(rows, 1U);
    if (node.texturePreview.enabled) {
        rows = std::max<std::size_t>(rows, 7U);
    }
    return HeaderHeight + BodyTopPadding + (PinRowHeight * static_cast<float>(rows)) + BodyBottomPadding;
}

MaterialGraphCanvasRect MaterialGraphCanvas::NodeBoundsWorld(const MaterialGraphCanvasNode& node) const noexcept {
    return MaterialGraphCanvasRect{ node.x, node.y, NodeWidth(node), NodeHeight(node) };
}

MaterialGraphCanvasPoint MaterialGraphCanvas::WorldToLocal(MaterialGraphCanvasPoint world) const noexcept {
    return MaterialGraphCanvasPoint{ (world.x - panX_) * zoom_, (world.y - panY_) * zoom_ };
}

MaterialGraphCanvasPoint MaterialGraphCanvas::LocalToWorld(MaterialGraphCanvasPoint local) const noexcept {
    return MaterialGraphCanvasPoint{ (local.x / zoom_) + panX_, (local.y / zoom_) + panY_ };
}

MaterialGraphCanvasPoint MaterialGraphCanvas::WindowToLocal(float windowX, float windowY) const noexcept {
    return MaterialGraphCanvasPoint{ windowX - viewport_.x, windowY - viewport_.y };
}

MaterialGraphCanvasPoint MaterialGraphCanvas::WindowToWorld(float windowX, float windowY) const noexcept {
    return LocalToWorld(WindowToLocal(windowX, windowY));
}

MaterialGraphCanvasPoint MaterialGraphCanvas::PinCenterWorld(
    const MaterialGraphCanvasNode& node,
    std::uint32_t pin,
    bool output) const noexcept {
    const float x = output ? node.x + NodeWidth(node) : node.x;
    const float height = NodeHeight(node);
    const auto centeredY = [height](std::uint32_t row, std::size_t count) noexcept {
        const float safeCount = static_cast<float>(std::max<std::size_t>(1U, count));
        const float bodyHeight = std::max(1.0F, height - HeaderHeight);
        const float rowHeight = std::min(PinRowHeight, std::max(16.0F, bodyHeight / safeCount));
        const float total = safeCount * rowHeight;
        return HeaderHeight + (bodyHeight * 0.5F) - (total * 0.5F) +
            (static_cast<float>(row) * rowHeight) + (rowHeight * 0.5F);
    };
    float y = 0.0F;
    if (output && node.outputsPerField) {
        y = HeaderHeight + BodyTopPadding + (PinRowHeight * (static_cast<float>(pin) + 0.5F));
    } else if (node.alignPinRowsAcrossLanes) {
        const std::size_t rowCount = std::max(node.inputs.size(), node.outputs.size());
        const std::uint32_t row = output && node.outputs.size() == 1U && rowCount > 1U
            ? static_cast<std::uint32_t>(rowCount / 2U)
            : pin;
        y = centeredY(row, rowCount);
    } else if (output && !node.valueFields.empty() && node.outputs.size() > 1U) {
        y = HeaderHeight + BodyTopPadding + (PinRowHeight * (static_cast<float>(pin) + 0.5F));
    } else if (output && node.texturePreview.enabled) {
        const float previewTop = HeaderHeight + 8.0F;
        const float previewBottom = std::max(previewTop + 1.0F, height - 10.0F);
        const float previewHeight = previewBottom - previewTop;
        const float safeCount = static_cast<float>(std::max<std::size_t>(1U, node.outputs.size()));
        const float rowHeight = std::min(PinRowHeight, std::max(16.0F, previewHeight / safeCount));
        const float total = safeCount * rowHeight;
        y = previewTop + (previewHeight * 0.5F) - (total * 0.5F) +
            (static_cast<float>(pin) * rowHeight) + (rowHeight * 0.5F);
    } else {
        y = centeredY(pin, output ? node.outputs.size() : node.inputs.size());
    }
    return MaterialGraphCanvasPoint{ x, node.y + y };
}

MaterialGraphCanvasPoint MaterialGraphCanvas::PinCenterWorld(
    std::uint32_t node,
    std::uint32_t pin,
    bool output) const noexcept {
    if (!ValidIndex(node, nodes_.size())) {
        return {};
    }
    const MaterialGraphCanvasNode& graphNode = nodes_[node];
    const std::size_t pinCount = output ? graphNode.outputs.size() : graphNode.inputs.size();
    if (!ValidIndex(pin, pinCount)) {
        return {};
    }
    return PinCenterWorld(graphNode, pin, output);
}

MaterialGraphCanvasPoint MaterialGraphCanvas::PinCenterWindow(
    std::uint32_t node,
    std::uint32_t pin,
    bool output) const noexcept {
    if (!ValidIndex(node, nodes_.size())) {
        return {};
    }
    const MaterialGraphCanvasNode& graphNode = nodes_[node];
    const std::size_t pinCount = output ? graphNode.outputs.size() : graphNode.inputs.size();
    if (!ValidIndex(pin, pinCount)) {
        return {};
    }
    const MaterialGraphCanvasPoint local = WorldToLocal(PinCenterWorld(graphNode, pin, output));
    return MaterialGraphCanvasPoint{ viewport_.x + local.x, viewport_.y + local.y };
}

std::optional<MaterialGraphCanvasPinHit> MaterialGraphCanvas::HitTestPinLocal(
    MaterialGraphCanvasPoint local,
    const MaterialGraphCanvasPinPredicate& predicate) const {
    if (!PointInsideViewportLocal(local)) {
        return std::nullopt;
    }
    const float radius = PinRadius * zoom_;
    const float rowHalf = std::max(radius + 4.0F, (PinRowHeight * zoom_) * 0.65F);
    const float pad = radius + 12.0F;

    for (std::size_t nodeIndex = nodes_.size(); nodeIndex-- > 0U;) {
        const MaterialGraphCanvasNode& node = nodes_[nodeIndex];
        const MaterialGraphCanvasPoint origin = WorldToLocal(MaterialGraphCanvasPoint{ node.x, node.y });
        const float screenWidth = NodeWidth(node) * zoom_;
        const float screenHeight = NodeHeight(node) * zoom_;
        const float bx = local.x - origin.x;
        const float by = local.y - origin.y;
        if (bx < -pad || bx > screenWidth + pad || by < -pad || by > screenHeight + pad) {
            continue;
        }

        const float center = screenWidth * 0.5F;
        const float texturePinLaneMax = screenWidth * 0.24F;
        const float texturePinLaneMin = std::min(72.0F * zoom_, texturePinLaneMax);
        const float texturePinLaneWidth = node.texturePreview.enabled
            ? std::clamp(screenWidth * 0.14F, texturePinLaneMin, texturePinLaneMax)
            : 0.0F;
        // A pin only answers clicks in its own half of the node; the other half drags the node, the
        // same as its title bar. Without this an input-only node (Material Output) pulled a wire out of
        // every click anywhere on its body.
        std::optional<MaterialGraphCanvasPinHit> bestHit;
        float bestDistanceSquared = std::numeric_limits<float>::max();
        const auto consider = [&](std::size_t pin, bool output, bool horizontallyEligible, float verticalHalf) {
            const MaterialGraphCanvasPoint pinWorld = PinCenterWorld(
                node,
                static_cast<std::uint32_t>(pin),
                output);
            const MaterialGraphCanvasPoint centerLocal = WorldToLocal(pinWorld);
            if (!PointInsideViewportLocal(centerLocal) ||
                std::abs(by - ((pinWorld.y - node.y) * zoom_)) > verticalHalf ||
                !horizontallyEligible) {
                return;
            }
            const MaterialGraphCanvasPinHit candidate{
                static_cast<std::uint32_t>(nodeIndex),
                static_cast<std::uint32_t>(pin),
                output,
            };
            if (predicate && !predicate(candidate)) {
                return;
            }
            const float dx = local.x - centerLocal.x;
            const float dy = local.y - centerLocal.y;
            const float distanceSquared = (dx * dx) + (dy * dy);
            if (!bestHit.has_value() || distanceSquared < bestDistanceSquared) {
                bestHit = candidate;
                bestDistanceSquared = distanceSquared;
            }
        };

        for (std::size_t pin = 0U; pin < node.inputs.size(); ++pin) {
            const MaterialGraphCanvasPoint pinWorld = PinCenterWorld(
                node,
                static_cast<std::uint32_t>(pin),
                false);
            const float cy = (pinWorld.y - node.y) * zoom_;
            if (std::abs(by - cy) > rowHalf) {
                continue;
            }
            consider(
                pin,
                false,
                node.texturePreview.enabled ? bx <= texturePinLaneWidth : bx <= center,
                rowHalf);
        }

        for (std::size_t pin = 0U; pin < node.outputs.size(); ++pin) {
            const MaterialGraphCanvasPoint pinWorld = PinCenterWorld(
                node,
                static_cast<std::uint32_t>(pin),
                true);
            const float cy = (pinWorld.y - node.y) * zoom_;
            if (std::abs(by - cy) > rowHalf) {
                continue;
            }
            consider(
                pin,
                true,
                node.texturePreview.enabled ? bx >= screenWidth - texturePinLaneWidth : bx >= center,
                rowHalf);
        }
        if (bestHit.has_value()) {
            return bestHit;
        }
    }
    return std::nullopt;
}

std::optional<std::uint32_t> MaterialGraphCanvas::HitTestNodeLocal(MaterialGraphCanvasPoint local) const noexcept {
    for (std::size_t nodeIndex = nodes_.size(); nodeIndex-- > 0U;) {
        const MaterialGraphCanvasNode& node = nodes_[nodeIndex];
        const MaterialGraphCanvasPoint origin = WorldToLocal(MaterialGraphCanvasPoint{ node.x, node.y });
        const float width = NodeWidth(node) * zoom_;
        const float height = NodeHeight(node) * zoom_;
        if (local.x >= origin.x && local.x < origin.x + width &&
            local.y >= origin.y && local.y < origin.y + height) {
            return static_cast<std::uint32_t>(nodeIndex);
        }
    }
    return std::nullopt;
}

std::optional<MaterialGraphCanvasPinHit> MaterialGraphCanvas::HitTestPin(
    float windowX,
    float windowY,
    const MaterialGraphCanvasPinPredicate& predicate) const {
    return HitTestPinLocal(WindowToLocal(windowX, windowY), predicate);
}

std::optional<std::uint32_t> MaterialGraphCanvas::HitTestNode(float windowX, float windowY) const noexcept {
    return HitTestNodeLocal(WindowToLocal(windowX, windowY));
}

std::optional<std::uint32_t> MaterialGraphCanvas::HitTestLinkLocal(MaterialGraphCanvasPoint local) const noexcept {
    if (!PointInsideViewportLocal(local) || PointOccludedForLinks(local)) {
        return std::nullopt;
    }
    float bestDistance = std::numeric_limits<float>::max();
    std::optional<std::uint32_t> bestLink;
    for (std::size_t linkIndex = 0U; linkIndex < links_.size(); ++linkIndex) {
        const MaterialGraphCanvasLink& link = links_[linkIndex];
        if (!ValidIndex(link.fromNode, nodes_.size()) || !ValidIndex(link.toNode, nodes_.size())) {
            continue;
        }
        const MaterialGraphCanvasPoint from = WorldToLocal(PinCenterWorld(link.fromNode, link.fromPin, true));
        const MaterialGraphCanvasPoint to = WorldToLocal(PinCenterWorld(link.toNode, link.toPin, false));
        const float dx = std::max(48.0F * zoom_, std::abs(to.x - from.x) * 0.5F);
        const MaterialGraphCanvasPoint c1{ from.x + dx, from.y };
        const MaterialGraphCanvasPoint c2{ to.x - dx, to.y };
        const float distance = DistancePointBezier(local, from, c1, c2, to);
        if (distance < bestDistance) {
            bestDistance = distance;
            bestLink = static_cast<std::uint32_t>(linkIndex);
        }
    }
    const float threshold = std::max(8.0F, 5.0F * zoom_);
    if (bestLink.has_value() && bestDistance <= threshold) {
        return bestLink;
    }
    return std::nullopt;
}

bool MaterialGraphCanvas::PointInsideViewportLocal(MaterialGraphCanvasPoint local) const noexcept {
    return local.x >= 0.0F && local.y >= 0.0F &&
        local.x < viewport_.width && local.y < viewport_.height;
}

bool MaterialGraphCanvas::PointOccludedForLinks(MaterialGraphCanvasPoint local) const noexcept {
    const MaterialGraphCanvasPoint world = LocalToWorld(local);
    const auto contains = [world](const MaterialGraphCanvasRect& rect) noexcept {
        return world.x > rect.x && world.x < rect.x + rect.width &&
            world.y > rect.y && world.y < rect.y + rect.height;
    };
    for (const MaterialGraphCanvasNode& node : nodes_) {
        if (contains(NodeBoundsWorld(node))) {
            return true;
        }
    }
    return std::ranges::any_of(occluders_, contains);
}

std::optional<std::uint32_t> MaterialGraphCanvas::HitTestLink(float windowX, float windowY) const noexcept {
    return HitTestLinkLocal(WindowToLocal(windowX, windowY));
}


} // namespace kb::editor
