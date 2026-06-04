#include "kb/render/frame/RenderPassGraph.hpp"

#include "frame/RenderPassGraphCompiler.hpp"
#include "frame/RenderPassGraphValidator.hpp"

#include <algorithm>

namespace kb::render {
namespace {

[[nodiscard]] constexpr std::uint8_t PassKindIndex(RenderPassKind kind) noexcept {
    return static_cast<std::uint8_t>(kind);
}

} // namespace

const char* RenderPassGraphValidationStatusName(RenderPassGraphValidationStatus status) noexcept {
    switch (status) {
    case RenderPassGraphValidationStatus::Success:
        return "Success";
    case RenderPassGraphValidationStatus::EmptyGraph:
        return "EmptyGraph";
    case RenderPassGraphValidationStatus::MissingRequiredPass:
        return "MissingRequiredPass";
    case RenderPassGraphValidationStatus::DisabledRequiredPass:
        return "DisabledRequiredPass";
    case RenderPassGraphValidationStatus::InvalidPassId:
        return "InvalidPassId";
    case RenderPassGraphValidationStatus::InvalidView:
        return "InvalidView";
    case RenderPassGraphValidationStatus::InvalidResource:
        return "InvalidResource";
    case RenderPassGraphValidationStatus::MissingResourceDeclaration:
        return "MissingResourceDeclaration";
    case RenderPassGraphValidationStatus::ReadBeforeWrite:
        return "ReadBeforeWrite";
    case RenderPassGraphValidationStatus::PassWithoutOutput:
        return "PassWithoutOutput";
    }
    return "Unknown";
}

bool RenderPassGraph::AddResource(RenderGraphResourceDesc resource) {
    if (!resource.IsValid() || resource.id.value >= declaredResources_.size() || declaredResources_[resource.id.value]) {
        return false;
    }
    declaredResources_[resource.id.value] = true;
    resources_.push_back(resource);
    return true;
}

bool RenderPassGraph::AddPass(RenderPassDesc pass) {
    const std::uint8_t kindIndex = PassKindIndex(pass.kind);
    if (kindIndex >= usedKinds_.size() || usedKinds_[kindIndex] || !pass.HasValidView()) {
        return false;
    }

    if (pass.emitsBgfxView) {
        if (usedViews_[pass.viewId]) {
            return false;
        }
        usedViews_[pass.viewId] = true;
    }

    usedKinds_[kindIndex] = true;
    passes_.push_back(pass);
    viewOrderDirty_ = true;
    return true;
}

void RenderPassGraph::Clear() noexcept {
    std::ranges::fill(usedKinds_, false);
    std::ranges::fill(usedViews_, false);
    std::ranges::fill(declaredResources_, false);
    resources_.clear();
    passes_.clear();
    viewOrder_.clear();
    viewOrderDirty_ = true;
}

std::span<const RenderGraphResourceDesc> RenderPassGraph::Resources() const noexcept {
    return resources_;
}

const RenderGraphResourceDesc* RenderPassGraph::FindResource(RenderGraphResourceId resource) const noexcept {
    const auto iter = std::ranges::find_if(resources_, [resource](const RenderGraphResourceDesc& desc) {
        return desc.id == resource;
    });
    return iter == resources_.end() ? nullptr : &(*iter);
}

std::span<const RenderPassDesc> RenderPassGraph::Passes() const noexcept {
    return passes_;
}

const RenderPassDesc* RenderPassGraph::FindPass(RenderPassKind kind) const noexcept {
    const auto iter = std::ranges::find_if(passes_, [kind](const RenderPassDesc& pass) {
        return pass.kind == kind;
    });
    return iter == passes_.end() ? nullptr : &(*iter);
}

bool RenderPassGraph::HasPass(RenderPassKind kind) const noexcept {
    return FindPass(kind) != nullptr;
}

RenderPassGraphValidationResult RenderPassGraph::ValidateRequiredPasses() const noexcept {
    return RenderPassGraphValidator::Validate(resources_, passes_);
}

RenderPassGraphCompileResult RenderPassGraph::Compile() const {
    return RenderPassGraphCompiler::Compile(resources_, passes_, ValidateRequiredPasses());
}

std::span<const std::uint16_t> RenderPassGraph::ViewOrder() {
    if (!viewOrderDirty_) {
        return viewOrder_;
    }

    viewOrder_.clear();
    viewOrder_.reserve(passes_.size());
    for (const RenderPassDesc& pass : passes_) {
        if (pass.enabled && pass.emitsBgfxView) {
            viewOrder_.push_back(pass.viewId);
        }
    }
    viewOrderDirty_ = false;
    return viewOrder_;
}

} // namespace kb::render
