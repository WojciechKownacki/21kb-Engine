#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace kb::editor {

enum class MaterialGraphCanvasPinType : std::uint8_t {
    Float,
    Float2,
    Float3,
    Float4,
    Color,
    Texture,
    Bool,
    MaterialAttributes,
};

struct MaterialGraphCanvasColor final {
    float r{ 1.0F };
    float g{ 1.0F };
    float b{ 1.0F };
    float a{ 1.0F };
};

struct MaterialGraphCanvasPoint final {
    float x{ 0.0F };
    float y{ 0.0F };
};

struct MaterialGraphCanvasRect final {
    float x{ 0.0F };
    float y{ 0.0F };
    float width{ 0.0F };
    float height{ 0.0F };
};

struct MaterialGraphCanvasPin final {
    std::string label;
    std::string stableId;
    MaterialGraphCanvasPinType type{ MaterialGraphCanvasPinType::Float };
    bool hasCustomColor{ false };
    MaterialGraphCanvasColor customColor{};
};

struct MaterialGraphCanvasValueField final {
    bool editable{ true };
    std::string label;
    std::string text;
    bool isColorSwatch{ false };
    MaterialGraphCanvasColor swatchColor{};
    std::vector<std::string> rgbaLabels;
    std::vector<std::string> rgbaTexts;
    int componentIndex{ -1 };
    int rowSpan{ 1 };
};

struct MaterialGraphCanvasTexturePreview final {
    bool enabled{ false };
    std::string stableId;
    std::vector<std::uint8_t> rgba8;
    std::uint32_t width{ 0U };
    std::uint32_t height{ 0U };
};

struct MaterialGraphCanvasNode final {
    std::string title;
    std::string stableId;
    float x{ 0.0F };
    float y{ 0.0F };
    std::vector<MaterialGraphCanvasPin> inputs;
    std::vector<MaterialGraphCanvasPin> outputs;
    MaterialGraphCanvasColor headerColor{ 0.18F, 0.42F, 0.42F, 1.0F };
    bool isOutput{ false };
    std::vector<MaterialGraphCanvasValueField> valueFields;
    bool outputsPerField{ false };
    bool alignPinRowsAcrossLanes{ false };
    float widthOverride{ 0.0F };
    float heightOverride{ 0.0F };
    MaterialGraphCanvasTexturePreview texturePreview{};
};

struct MaterialGraphCanvasLink final {
    std::uint32_t fromNode{ 0U };
    std::uint32_t fromPin{ 0U };
    std::uint32_t toNode{ 0U };
    std::uint32_t toPin{ 0U };
    std::string stableId;
};

struct MaterialGraphCanvasPinHit final {
    std::uint32_t node{ 0U };
    std::uint32_t pin{ 0U };
    bool output{ false };
};

using MaterialGraphCanvasPinPredicate = std::function<bool(const MaterialGraphCanvasPinHit&)>;

class MaterialGraphCanvas final {
public:
    static constexpr float DefaultNodeWidth = 204.0F;
    static constexpr float HeaderHeight = 30.0F;
    static constexpr float BodyTopPadding = 10.0F;
    static constexpr float BodyBottomPadding = 12.0F;
    static constexpr float PinRowHeight = 24.0F;
    static constexpr float PinRadius = 6.0F;

    [[nodiscard]] std::uint32_t AddNode(MaterialGraphCanvasNode node);
    void AddLink(MaterialGraphCanvasLink link);
    void AddOccluderWorld(MaterialGraphCanvasRect rect);
    void Clear() noexcept;

    void SetViewport(MaterialGraphCanvasRect viewport) noexcept;
    void SetView(float panX, float panY, float zoom) noexcept;
    [[nodiscard]] MaterialGraphCanvasRect Viewport() const noexcept;
    [[nodiscard]] float PanX() const noexcept;
    [[nodiscard]] float PanY() const noexcept;
    [[nodiscard]] float Zoom() const noexcept;

    [[nodiscard]] std::size_t NodeCount() const noexcept;
    [[nodiscard]] std::size_t LinkCount() const noexcept;

    [[nodiscard]] const std::vector<MaterialGraphCanvasNode>& Nodes() const noexcept;
    [[nodiscard]] const std::vector<MaterialGraphCanvasLink>& Links() const noexcept;
    [[nodiscard]] const MaterialGraphCanvasNode* NodeAt(std::uint32_t node) const noexcept;
    [[nodiscard]] MaterialGraphCanvasNode* MutableNodeAt(std::uint32_t node) noexcept;

    [[nodiscard]] float NodeWidth(const MaterialGraphCanvasNode& node) const noexcept;
    [[nodiscard]] float NodeHeight(const MaterialGraphCanvasNode& node) const noexcept;
    [[nodiscard]] MaterialGraphCanvasRect NodeBoundsWorld(const MaterialGraphCanvasNode& node) const noexcept;

    [[nodiscard]] MaterialGraphCanvasPoint WorldToLocal(MaterialGraphCanvasPoint world) const noexcept;
    [[nodiscard]] MaterialGraphCanvasPoint LocalToWorld(MaterialGraphCanvasPoint local) const noexcept;
    [[nodiscard]] MaterialGraphCanvasPoint WindowToLocal(float windowX, float windowY) const noexcept;
    [[nodiscard]] MaterialGraphCanvasPoint WindowToWorld(float windowX, float windowY) const noexcept;
    [[nodiscard]] MaterialGraphCanvasPoint PinCenterWorld(
        std::uint32_t node,
        std::uint32_t pin,
        bool output) const noexcept;
    [[nodiscard]] MaterialGraphCanvasPoint PinCenterWindow(
        std::uint32_t node,
        std::uint32_t pin,
        bool output) const noexcept;

    [[nodiscard]] std::optional<MaterialGraphCanvasPinHit> HitTestPin(
        float windowX,
        float windowY,
        const MaterialGraphCanvasPinPredicate& predicate = {}) const;
    [[nodiscard]] std::optional<std::uint32_t> HitTestNode(float windowX, float windowY) const noexcept;
    [[nodiscard]] std::optional<std::uint32_t> HitTestLink(float windowX, float windowY) const noexcept;

private:
    [[nodiscard]] float TotalValueFieldRows(const MaterialGraphCanvasNode& node) const noexcept;
    [[nodiscard]] MaterialGraphCanvasPoint PinCenterWorld(
        const MaterialGraphCanvasNode& node,
        std::uint32_t pin,
        bool output) const noexcept;
    [[nodiscard]] std::optional<MaterialGraphCanvasPinHit> HitTestPinLocal(
        MaterialGraphCanvasPoint local,
        const MaterialGraphCanvasPinPredicate& predicate = {}) const;
    [[nodiscard]] std::optional<std::uint32_t> HitTestNodeLocal(MaterialGraphCanvasPoint local) const noexcept;
    [[nodiscard]] std::optional<std::uint32_t> HitTestLinkLocal(MaterialGraphCanvasPoint local) const noexcept;
    [[nodiscard]] bool PointInsideViewportLocal(MaterialGraphCanvasPoint local) const noexcept;
    [[nodiscard]] bool PointOccludedForLinks(MaterialGraphCanvasPoint local) const noexcept;
    std::vector<MaterialGraphCanvasNode> nodes_;
    std::vector<MaterialGraphCanvasLink> links_;
    std::vector<MaterialGraphCanvasRect> occluders_;
    MaterialGraphCanvasRect viewport_{};
    float panX_{ 0.0F };
    float panY_{ 0.0F };
    float zoom_{ 1.0F };
};

} // namespace kb::editor
