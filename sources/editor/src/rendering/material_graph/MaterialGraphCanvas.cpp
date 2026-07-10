#include "rendering/material_graph/MaterialGraphCanvas.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>
#include <string>
#include <utility>

namespace kb::editor {
namespace {

constexpr float kMinZoom = 0.30F;
constexpr float kMaxZoom = 2.50F;

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

[[nodiscard]] bool RectsIntersect(MaterialGraphCanvasRect lhs, MaterialGraphCanvasRect rhs) noexcept {
    return lhs.x < rhs.x + rhs.width &&
        lhs.x + lhs.width > rhs.x &&
        lhs.y < rhs.y + rhs.height &&
        lhs.y + lhs.height > rhs.y;
}

[[nodiscard]] MaterialGraphCanvasRect NormalizedRect(MaterialGraphCanvasPoint a, MaterialGraphCanvasPoint b) noexcept {
    const float left = std::min(a.x, b.x);
    const float top = std::min(a.y, b.y);
    const float right = std::max(a.x, b.x);
    const float bottom = std::max(a.y, b.y);
    return MaterialGraphCanvasRect{ left, top, right - left, bottom - top };
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
    selection_.clear();
    dragOrigins_.clear();
    marqueeBaseSelection_.clear();
    drag_ = DragMode::None;
}

void MaterialGraphCanvas::SetBound(bool bound) noexcept {
    bound_ = bound;
}

bool MaterialGraphCanvas::Bound() const noexcept {
    return bound_;
}

void MaterialGraphCanvas::SetViewport(MaterialGraphCanvasRect viewport) noexcept {
    viewport_ = viewport;
}

void MaterialGraphCanvas::SetView(float panX, float panY, float zoom) noexcept {
    panX_ = panX;
    panY_ = panY;
    zoom_ = std::clamp(zoom, kMinZoom, kMaxZoom);
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

std::size_t MaterialGraphCanvas::SelectedCount() const noexcept {
    return selection_.size();
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
    const MaterialGraphCanvasPoint local = WorldToLocal(PinCenterWorld(node, pin, output));
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
        const bool outputUsesValueRows = node.outputsPerField ||
            (node.inputs.empty() && !node.valueFields.empty() && node.outputs.size() == 1U) ||
            (!node.valueFields.empty() && node.outputs.size() > 1U);
        const auto hasInputOnRow = [&](float cy) noexcept {
            for (std::size_t otherPin = 0U; otherPin < node.inputs.size(); ++otherPin) {
                const MaterialGraphCanvasPoint otherWorld = PinCenterWorld(
                    node,
                    static_cast<std::uint32_t>(otherPin),
                    false);
                const float otherCy = (otherWorld.y - node.y) * zoom_;
                if (std::abs(otherCy - cy) <= rowHalf) {
                    return true;
                }
            }
            return false;
        };
        const auto hasOutputOnRow = [&](float cy) noexcept {
            for (std::size_t otherPin = 0U; otherPin < node.outputs.size(); ++otherPin) {
                const MaterialGraphCanvasPoint otherWorld = PinCenterWorld(
                    node,
                    static_cast<std::uint32_t>(otherPin),
                    true);
                const float otherCy = (otherWorld.y - node.y) * zoom_;
                if (std::abs(otherCy - cy) <= rowHalf) {
                    return true;
                }
            }
            return false;
        };
        std::optional<MaterialGraphCanvasPinHit> bestHit;
        float bestDistanceSquared = std::numeric_limits<float>::max();
        const auto consider = [&](std::size_t pin, bool output, bool horizontallyEligible, float verticalHalf) {
            const MaterialGraphCanvasPoint pinWorld = PinCenterWorld(
                node,
                static_cast<std::uint32_t>(pin),
                output);
            const MaterialGraphCanvasPoint centerLocal = WorldToLocal(pinWorld);
            if (!PointInsideViewportLocal(centerLocal) ||
                std::abs(by - (pinWorld.y - node.y) * zoom_) > verticalHalf ||
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
            const bool outputOnRow = hasOutputOnRow(cy);
            consider(
                pin,
                false,
                node.texturePreview.enabled ? bx <= texturePinLaneWidth : (!outputOnRow || bx <= center),
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
            const bool inputOnRow = hasInputOnRow(cy);
            const bool outputOnlyOwnsFullRow = !inputOnRow && !outputUsesValueRows;
            consider(
                pin,
                true,
                node.texturePreview.enabled ? bx >= screenWidth - texturePinLaneWidth : (outputOnlyOwnsFullRow || bx >= center),
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

bool MaterialGraphCanvas::IsSelected(std::uint32_t node) const noexcept {
    return std::find(selection_.begin(), selection_.end(), node) != selection_.end();
}

bool MaterialGraphCanvas::SelectNode(std::uint32_t node, bool additive, bool toggle) {
    if (!ValidIndex(node, nodes_.size())) {
        return false;
    }
    const bool selected = IsSelected(node);
    if (toggle) {
        if (selected) {
            selection_.erase(std::remove(selection_.begin(), selection_.end(), node), selection_.end());
        } else {
            selection_.push_back(node);
        }
        return true;
    }
    if (!additive && (selection_.size() != 1U || !selected)) {
        selection_.clear();
    }
    if (!selected) {
        selection_.push_back(node);
        return true;
    }
    return !additive;
}

void MaterialGraphCanvas::ClearSelection() noexcept {
    selection_.clear();
}

bool MaterialGraphCanvas::SelectAll() {
    selection_.clear();
    selection_.reserve(nodes_.size());
    for (std::size_t index = 0U; index < nodes_.size(); ++index) {
        selection_.push_back(static_cast<std::uint32_t>(index));
    }
    return !selection_.empty();
}

void MaterialGraphCanvas::BeginNodeDrag(MaterialGraphCanvasPoint local) {
    drag_ = DragMode::Node;
    pressLocal_ = local;
    lastLocal_ = local;
    dragOrigins_.clear();
    dragOrigins_.reserve(selection_.size());
    for (std::uint32_t node : selection_) {
        if (ValidIndex(node, nodes_.size())) {
            dragOrigins_.push_back(DragNodeOrigin{ node, nodes_[node].x, nodes_[node].y });
        }
    }
}

bool MaterialGraphCanvas::OnPointerDown(float windowX, float windowY, bool additive, bool toggle) {
    const MaterialGraphCanvasPoint local = WindowToLocal(windowX, windowY);
    pressLocal_ = local;
    lastLocal_ = local;

    if (const std::optional<MaterialGraphCanvasPinHit> hit = HitTestPinLocal(local)) {
        static_cast<void>(SelectNode(hit->node, false, false));
        connectStart_ = *hit;
        if (!hit->output) {
            if (const std::optional<std::uint32_t> existing = ExistingInputLink(hit->node, hit->pin)) {
                const MaterialGraphCanvasLink link = links_[*existing];
                connectStart_ = MaterialGraphCanvasPinHit{ link.fromNode, link.fromPin, true };
                RemoveLinkAt(*existing, true);
            }
        }
        connectLocal_ = local;
        drag_ = DragMode::Connect;
        return true;
    }

    if (const std::optional<std::uint32_t> node = HitTestNodeLocal(local)) {
        const bool wasSelected = IsSelected(*node);
        static_cast<void>(SelectNode(*node, additive, toggle));
        if (!toggle || wasSelected) {
            BeginNodeDrag(local);
        }
        return true;
    }

    if (!additive && !toggle) {
        selection_.clear();
    }
    marqueeBaseSelection_ = selection_;
    marqueeAdditive_ = additive;
    marqueeToggle_ = toggle;
    drag_ = DragMode::Marquee;
    return true;
}

bool MaterialGraphCanvas::OnRightPointerDown(float windowX, float windowY) {
    pressLocal_ = WindowToLocal(windowX, windowY);
    lastLocal_ = pressLocal_;
    drag_ = DragMode::Pan;
    return true;
}

bool MaterialGraphCanvas::OnPointerMove(float windowX, float windowY) {
    const MaterialGraphCanvasPoint local = WindowToLocal(windowX, windowY);
    if (drag_ == DragMode::None) {
        return false;
    }

    if (drag_ == DragMode::Pan) {
        const float dx = local.x - lastLocal_.x;
        const float dy = local.y - lastLocal_.y;
        panX_ -= dx / zoom_;
        panY_ -= dy / zoom_;
        lastLocal_ = local;
        return true;
    }

    if (drag_ == DragMode::Node) {
        const float dx = (local.x - pressLocal_.x) / zoom_;
        const float dy = (local.y - pressLocal_.y) / zoom_;
        for (const DragNodeOrigin& origin : dragOrigins_) {
            if (ValidIndex(origin.node, nodes_.size())) {
                nodes_[origin.node].x = origin.x + dx;
                nodes_[origin.node].y = origin.y + dy;
            }
        }
        lastLocal_ = local;
        return true;
    }

    if (drag_ == DragMode::Connect) {
        connectLocal_ = local;
        lastLocal_ = local;
        return true;
    }

    if (drag_ == DragMode::Marquee) {
        UpdateMarqueeSelection(local);
        lastLocal_ = local;
        return true;
    }

    return false;
}

bool MaterialGraphCanvas::OnPointerUp(float windowX, float windowY) {
    const MaterialGraphCanvasPoint local = WindowToLocal(windowX, windowY);
    const DragMode drag = drag_;
    drag_ = DragMode::None;
    if (drag == DragMode::Connect) {
        if (const std::optional<MaterialGraphCanvasPinHit> target = HitTestPinLocal(local)) {
            return TryConnectPins(connectStart_, *target);
        }
        return true;
    }
    if (drag == DragMode::Node) {
        bool moved = false;
        for (const DragNodeOrigin& origin : dragOrigins_) {
            if (!ValidIndex(origin.node, nodes_.size())) {
                continue;
            }
            const MaterialGraphCanvasNode& node = nodes_[origin.node];
            if (std::abs(node.x - origin.x) > 0.001F || std::abs(node.y - origin.y) > 0.001F) {
                moved = true;
                if (bound_) {
                    EmitMove(node);
                }
            }
        }
        dragOrigins_.clear();
        return moved;
    }
    if (drag == DragMode::Pan || drag == DragMode::Marquee) {
        return true;
    }
    return false;
}

bool MaterialGraphCanvas::TryConnectPins(MaterialGraphCanvasPinHit first, MaterialGraphCanvasPinHit second) {
    if (first.output == second.output) {
        return false;
    }
    const MaterialGraphCanvasPinHit from = first.output ? first : second;
    const MaterialGraphCanvasPinHit to = first.output ? second : first;
    if (from.node == to.node ||
        !ValidIndex(from.node, nodes_.size()) ||
        !ValidIndex(to.node, nodes_.size()) ||
        !ValidIndex(from.pin, nodes_[from.node].outputs.size()) ||
        !ValidIndex(to.pin, nodes_[to.node].inputs.size())) {
        return false;
    }

    for (std::size_t index = links_.size(); index-- > 0U;) {
        const MaterialGraphCanvasLink& link = links_[index];
        if (link.toNode == to.node && link.toPin == to.pin) {
            RemoveLinkAt(static_cast<std::uint32_t>(index), true);
        }
    }

    MaterialGraphCanvasLink link{
        .fromNode = from.node,
        .fromPin = from.pin,
        .toNode = to.node,
        .toPin = to.pin,
        .stableId = MakeGeneratedId("link"),
    };

    if (bound_) {
        emittedEdits_.push_back(MaterialGraphCanvasEdit{
            .kind = MaterialGraphCanvasEditKind::Connect,
            .fromNode = NodeStableId(from.node),
            .fromPin = PinStableId(from.node, from.pin, true),
            .toNode = NodeStableId(to.node),
            .toPin = PinStableId(to.node, to.pin, false),
        });
        return true;
    }

    links_.push_back(std::move(link));
    return true;
}

std::optional<std::uint32_t> MaterialGraphCanvas::ExistingInputLink(std::uint32_t node, std::uint32_t pin) const noexcept {
    for (std::size_t index = 0U; index < links_.size(); ++index) {
        if (links_[index].toNode == node && links_[index].toPin == pin) {
            return static_cast<std::uint32_t>(index);
        }
    }
    return std::nullopt;
}

void MaterialGraphCanvas::EmitDisconnect(const MaterialGraphCanvasLink& link) {
    emittedEdits_.push_back(MaterialGraphCanvasEdit{
        .kind = MaterialGraphCanvasEditKind::Disconnect,
        .fromNode = NodeStableId(link.fromNode),
        .fromPin = PinStableId(link.fromNode, link.fromPin, true),
        .toNode = NodeStableId(link.toNode),
        .toPin = PinStableId(link.toNode, link.toPin, false),
        .linkId = link.stableId,
    });
}

void MaterialGraphCanvas::EmitMove(const MaterialGraphCanvasNode& node) {
    emittedEdits_.push_back(MaterialGraphCanvasEdit{
        .kind = MaterialGraphCanvasEditKind::Move,
        .nodeId = node.stableId,
        .x = node.x,
        .y = node.y,
    });
}

void MaterialGraphCanvas::RemoveLinkAt(std::uint32_t link, bool emitEdit) {
    if (!ValidIndex(link, links_.size())) {
        return;
    }
    const MaterialGraphCanvasLink removed = links_[link];
    if (emitEdit) {
        EmitDisconnect(removed);
    }
    if (!bound_) {
        links_.erase(links_.begin() + static_cast<std::ptrdiff_t>(link));
    }
}

bool MaterialGraphCanvas::BreakLinkAt(float windowX, float windowY) {
    const std::optional<std::uint32_t> link = HitTestLink(windowX, windowY);
    if (!link.has_value()) {
        return false;
    }
    RemoveLinkAt(*link, true);
    return true;
}

void MaterialGraphCanvas::UpdateMarqueeSelection(MaterialGraphCanvasPoint local) {
    const MaterialGraphCanvasRect marqueeLocal = NormalizedRect(pressLocal_, local);
    std::vector<std::uint32_t> hits = marqueeBaseSelection_;
    if (!marqueeAdditive_ && !marqueeToggle_) {
        hits.clear();
    }

    for (std::size_t index = 0U; index < nodes_.size(); ++index) {
        const MaterialGraphCanvasNode& node = nodes_[index];
        const MaterialGraphCanvasPoint origin = WorldToLocal(MaterialGraphCanvasPoint{ node.x, node.y });
        const MaterialGraphCanvasRect bounds{
            origin.x,
            origin.y,
            NodeWidth(node) * zoom_,
            NodeHeight(node) * zoom_,
        };
        if (!RectsIntersect(marqueeLocal, bounds)) {
            continue;
        }
        const std::uint32_t nodeIndex = static_cast<std::uint32_t>(index);
        const bool already = std::find(hits.begin(), hits.end(), nodeIndex) != hits.end();
        if (marqueeToggle_) {
            if (already) {
                hits.erase(std::remove(hits.begin(), hits.end(), nodeIndex), hits.end());
            } else {
                hits.push_back(nodeIndex);
            }
        } else if (!already) {
            hits.push_back(nodeIndex);
        }
    }

    selection_ = std::move(hits);
}

bool MaterialGraphCanvas::DeleteSelected() {
    if (selection_.empty()) {
        return false;
    }
    if (bound_) {
        for (std::uint32_t node : selection_) {
            if (ValidIndex(node, nodes_.size()) && !nodes_[node].isOutput) {
                emittedEdits_.push_back(MaterialGraphCanvasEdit{
                    .kind = MaterialGraphCanvasEditKind::DeleteNode,
                    .nodeId = nodes_[node].stableId,
                });
            }
        }
        return true;
    }

    std::vector<int> remap(nodes_.size(), -1);
    std::vector<MaterialGraphCanvasNode> keptNodes;
    keptNodes.reserve(nodes_.size());
    for (std::size_t index = 0U; index < nodes_.size(); ++index) {
        const bool selected = IsSelected(static_cast<std::uint32_t>(index));
        if (selected && !nodes_[index].isOutput) {
            continue;
        }
        remap[index] = static_cast<int>(keptNodes.size());
        keptNodes.push_back(std::move(nodes_[index]));
    }

    std::vector<MaterialGraphCanvasLink> keptLinks;
    keptLinks.reserve(links_.size());
    for (MaterialGraphCanvasLink link : links_) {
        if (!ValidIndex(link.fromNode, remap.size()) || !ValidIndex(link.toNode, remap.size())) {
            continue;
        }
        const int from = remap[link.fromNode];
        const int to = remap[link.toNode];
        if (from < 0 || to < 0) {
            continue;
        }
        link.fromNode = static_cast<std::uint32_t>(from);
        link.toNode = static_cast<std::uint32_t>(to);
        keptLinks.push_back(std::move(link));
    }

    nodes_ = std::move(keptNodes);
    links_ = std::move(keptLinks);
    selection_.clear();
    return true;
}

bool MaterialGraphCanvas::CopySelected() {
    clipboardNodes_.clear();
    clipboardLinks_.clear();
    if (selection_.empty()) {
        return false;
    }

    std::vector<int> remap(nodes_.size(), -1);
    for (std::uint32_t node : selection_) {
        if (!ValidIndex(node, nodes_.size()) || nodes_[node].isOutput) {
            continue;
        }
        remap[node] = static_cast<int>(clipboardNodes_.size());
        clipboardNodes_.push_back(nodes_[node]);
    }

    for (MaterialGraphCanvasLink link : links_) {
        if (!ValidIndex(link.fromNode, remap.size()) || !ValidIndex(link.toNode, remap.size())) {
            continue;
        }
        const int from = remap[link.fromNode];
        const int to = remap[link.toNode];
        if (from < 0 || to < 0) {
            continue;
        }
        link.fromNode = static_cast<std::uint32_t>(from);
        link.toNode = static_cast<std::uint32_t>(to);
        clipboardLinks_.push_back(std::move(link));
    }
    return !clipboardNodes_.empty();
}

bool MaterialGraphCanvas::PasteClipboard() {
    if (bound_ || clipboardNodes_.empty()) {
        return false;
    }

    const std::uint32_t baseIndex = static_cast<std::uint32_t>(nodes_.size());
    selection_.clear();
    for (MaterialGraphCanvasNode node : clipboardNodes_) {
        node.x += PasteOffset;
        node.y += PasteOffset;
        node.stableId = MakeGeneratedId(node.stableId.empty() ? "node" : node.stableId);
        selection_.push_back(static_cast<std::uint32_t>(nodes_.size()));
        nodes_.push_back(std::move(node));
    }

    for (MaterialGraphCanvasLink link : clipboardLinks_) {
        link.fromNode += baseIndex;
        link.toNode += baseIndex;
        link.stableId = MakeGeneratedId(link.stableId.empty() ? "link" : link.stableId);
        links_.push_back(std::move(link));
    }
    return true;
}

bool MaterialGraphCanvas::DuplicateSelected() {
    if (bound_ || !CopySelected()) {
        return false;
    }
    return PasteClipboard();
}

bool MaterialGraphCanvas::HasClipboard() const noexcept {
    return !clipboardNodes_.empty();
}

std::vector<std::string> MaterialGraphCanvas::SelectedStableIds() const {
    std::vector<std::string> ids;
    ids.reserve(selection_.size());
    for (std::uint32_t node : selection_) {
        if (ValidIndex(node, nodes_.size()) && !nodes_[node].stableId.empty()) {
            ids.push_back(nodes_[node].stableId);
        }
    }
    return ids;
}

void MaterialGraphCanvas::SelectStableIds(const std::vector<std::string>& ids) {
    selection_.clear();
    for (std::size_t node = 0U; node < nodes_.size(); ++node) {
        if (std::find(ids.begin(), ids.end(), nodes_[node].stableId) != ids.end()) {
            selection_.push_back(static_cast<std::uint32_t>(node));
        }
    }
}

std::vector<MaterialGraphCanvasEdit> MaterialGraphCanvas::TakeEmittedEdits() {
    return std::exchange(emittedEdits_, std::vector<MaterialGraphCanvasEdit>{});
}

std::string MaterialGraphCanvas::MakeGeneratedId(std::string_view prefix) {
    std::string id{ prefix };
    if (!id.empty()) {
        id += ".";
    }
    id += std::to_string(generatedId_++);
    return id;
}

std::string MaterialGraphCanvas::NodeStableId(std::uint32_t node) const {
    if (!ValidIndex(node, nodes_.size())) {
        return {};
    }
    if (!nodes_[node].stableId.empty()) {
        return nodes_[node].stableId;
    }
    return std::to_string(node);
}

std::string MaterialGraphCanvas::PinStableId(std::uint32_t node, std::uint32_t pin, bool output) const {
    if (!ValidIndex(node, nodes_.size())) {
        return {};
    }
    const MaterialGraphCanvasNode& graphNode = nodes_[node];
    const std::vector<MaterialGraphCanvasPin>& pins = output ? graphNode.outputs : graphNode.inputs;
    if (!ValidIndex(pin, pins.size())) {
        return {};
    }
    if (!pins[pin].stableId.empty()) {
        return pins[pin].stableId;
    }
    return pins[pin].label;
}

} // namespace kb::editor
