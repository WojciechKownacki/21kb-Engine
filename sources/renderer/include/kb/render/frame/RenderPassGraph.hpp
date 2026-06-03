#pragma once

#include "kb/render/frame/RenderPassDesc.hpp"

#include <array>
#include <cstdint>
#include <span>
#include <vector>

namespace kb::render {

enum class RenderPassGraphValidationStatus : std::uint8_t {
    Success,
    EmptyGraph,
    MissingRequiredPass,
    DisabledRequiredPass,
    InvalidPassId,
    InvalidView,
    InvalidResource,
    MissingResourceDeclaration,
    ReadBeforeWrite,
    PassWithoutOutput,
};

enum class RenderGraphResourceAccess : std::uint8_t {
    Undefined,
    RenderTargetRead,
    RenderTargetWrite,
    DepthRead,
    DepthWrite,
    ShaderRead,
    ComputeRead,
    ComputeWrite,
    CopyRead,
    CopyWrite,
    Present,
};

enum class RenderGraphDebugView : std::uint8_t {
    None,
    ResourceLifetime,
    Aliasing,
    Barriers,
    PassCost,
};

struct RenderGraphResourceBarrier {
    RenderGraphResourceId resource{};
    RenderPassKind beforePass = RenderPassKind::SceneTargetSetup;
    RenderPassKind afterPass = RenderPassKind::SceneTargetSetup;
    RenderGraphResourceAccess beforeAccess = RenderGraphResourceAccess::Undefined;
    RenderGraphResourceAccess afterAccess = RenderGraphResourceAccess::Undefined;
};

struct RenderGraphResourceAlias {
    RenderGraphResourceId resource{};
    std::uint16_t aliasSlot = 0xFFFFU;
    std::uint64_t byteSize = 0;
};

struct RenderGraphPassProfile {
    RenderPassKind pass = RenderPassKind::SceneTargetSetup;
    std::uint16_t viewId = 0;
    std::uint16_t readCount = 0;
    std::uint16_t writeCount = 0;
    std::uint64_t estimatedTargetBytes = 0;
    bool emitsBgfxView = false;
};

struct RenderPassGraphValidationResult {
    RenderPassGraphValidationStatus status = RenderPassGraphValidationStatus::EmptyGraph;
    RenderPassKind pass = RenderPassKind::SceneTargetSetup;
    RenderGraphResourceId resource{};

    [[nodiscard]] constexpr bool Succeeded() const noexcept {
        return status == RenderPassGraphValidationStatus::Success;
    }
};

struct RenderGraphResourceUsage {
    RenderGraphResourceId resource{};
    std::uint16_t firstPassIndex = 0xFFFFU;
    std::uint16_t lastPassIndex = 0;
    std::uint16_t readCount = 0;
    std::uint16_t writeCount = 0;
};

struct RenderPassGraphCompileResult {
    RenderPassGraphValidationResult validation{};
    std::vector<RenderGraphResourceUsage> resourceUsages;
    std::vector<RenderGraphResourceBarrier> barriers;
    std::vector<RenderGraphResourceAlias> aliases;
    std::vector<RenderGraphPassProfile> passProfiles;
    std::uint64_t estimatedTransientBytes = 0;
    std::uint64_t estimatedAliasedTransientBytes = 0;
    std::uint64_t transientAliasingSavingsBytes = 0;
    std::uint32_t transientResourceCount = 0;
    std::uint32_t externalResourceCount = 0;
    RenderGraphDebugView debugView = RenderGraphDebugView::None;

    [[nodiscard]] bool Succeeded() const noexcept {
        return validation.Succeeded();
    }
};

[[nodiscard]] const char* RenderPassGraphValidationStatusName(RenderPassGraphValidationStatus status) noexcept;

class RenderPassGraph {
public:
    [[nodiscard]] bool AddResource(RenderGraphResourceDesc resource);
    [[nodiscard]] bool AddPass(RenderPassDesc pass);
    void Clear() noexcept;

    [[nodiscard]] std::span<const RenderGraphResourceDesc> Resources() const noexcept;
    [[nodiscard]] const RenderGraphResourceDesc* FindResource(RenderGraphResourceId resource) const noexcept;
    [[nodiscard]] std::span<const RenderPassDesc> Passes() const noexcept;
    [[nodiscard]] const RenderPassDesc* FindPass(RenderPassKind kind) const noexcept;
    [[nodiscard]] bool HasPass(RenderPassKind kind) const noexcept;
    [[nodiscard]] RenderPassGraphValidationResult ValidateRequiredPasses() const noexcept;
    [[nodiscard]] RenderPassGraphCompileResult Compile() const;
    [[nodiscard]] std::span<const std::uint16_t> ViewOrder();

private:
    std::array<bool, RenderPassKindCount> usedKinds_{};
    std::array<bool, ViewId::Max> usedViews_{};
    std::array<bool, RenderGraphResource::Max> declaredResources_{};
    std::vector<RenderGraphResourceDesc> resources_;
    std::vector<RenderPassDesc> passes_;
    std::vector<std::uint16_t> viewOrder_;
    bool viewOrderDirty_ = true;
};

} // namespace kb::render
