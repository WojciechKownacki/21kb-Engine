#include "kb/render/frame/RenderPassGraph.hpp"

#include <algorithm>

namespace kb::render {
namespace {

[[nodiscard]] constexpr std::uint8_t PassKindIndex(RenderPassKind kind) noexcept {
    return static_cast<std::uint8_t>(kind);
}

[[nodiscard]] constexpr std::uint32_t BytesPerPixel(RenderTargetFormat format) noexcept {
    switch (format) {
    case RenderTargetFormat::Backbuffer:
    case RenderTargetFormat::Bgra8:
    case RenderTargetFormat::Rgba8:
    case RenderTargetFormat::D32:
    case RenderTargetFormat::D32F:
        return 4U;
    case RenderTargetFormat::Rgba16:
    case RenderTargetFormat::Rgba16F:
        return 8U;
    case RenderTargetFormat::R8:
        return 1U;
    case RenderTargetFormat::D24S8:
        return 4U;
    case RenderTargetFormat::Unknown:
        return 0U;
    }
    return 0U;
}

[[nodiscard]] std::uint64_t ResourceBytes(const RenderGraphResourceDesc& resource) noexcept {
    return static_cast<std::uint64_t>(resource.target.extent.width) *
        static_cast<std::uint64_t>(resource.target.extent.height) *
        static_cast<std::uint64_t>(BytesPerPixel(resource.target.format));
}

[[nodiscard]] bool IsDepthFormat(RenderTargetFormat format) noexcept {
    return format == RenderTargetFormat::D32 || format == RenderTargetFormat::D32F || format == RenderTargetFormat::D24S8;
}

[[nodiscard]] RenderGraphResourceAccess ReadAccessFor(const RenderGraphResourceDesc& resource) noexcept {
    if (resource.id == RenderGraphResource::FinalOutput) {
        return RenderGraphResourceAccess::Present;
    }
    return IsDepthFormat(resource.target.format) ? RenderGraphResourceAccess::DepthRead : RenderGraphResourceAccess::ShaderRead;
}

[[nodiscard]] RenderGraphResourceAccess WriteAccessFor(const RenderGraphResourceDesc& resource) noexcept {
    return IsDepthFormat(resource.target.format) ? RenderGraphResourceAccess::DepthWrite : RenderGraphResourceAccess::RenderTargetWrite;
}

[[nodiscard]] bool AccessNeedsBarrier(RenderGraphResourceAccess before, RenderGraphResourceAccess after) noexcept {
    if (before == RenderGraphResourceAccess::Undefined || before == after) {
        return false;
    }
    return true;
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
    if (passes_.empty()) {
        return RenderPassGraphValidationResult{
            .status = RenderPassGraphValidationStatus::EmptyGraph,
        };
    }

    for (const RenderPassDesc& pass : passes_) {
        if (!pass.id.IsValid()) {
            return RenderPassGraphValidationResult{
                .status = RenderPassGraphValidationStatus::InvalidPassId,
                .pass = pass.kind,
            };
        }
        if (!pass.HasValidView()) {
            return RenderPassGraphValidationResult{
                .status = RenderPassGraphValidationStatus::InvalidView,
                .pass = pass.kind,
            };
        }
        if (pass.enabled && pass.emitsBgfxView && pass.writes.empty() && !pass.neverCull) {
            return RenderPassGraphValidationResult{
                .status = RenderPassGraphValidationStatus::PassWithoutOutput,
                .pass = pass.kind,
            };
        }
        for (const RenderGraphResourceId resource : pass.reads) {
            if (!resource.IsValid()) {
                return RenderPassGraphValidationResult{
                    .status = RenderPassGraphValidationStatus::InvalidResource,
                    .pass = pass.kind,
                    .resource = resource,
                };
            }
            if (FindResource(resource) == nullptr) {
                return RenderPassGraphValidationResult{
                    .status = RenderPassGraphValidationStatus::MissingResourceDeclaration,
                    .pass = pass.kind,
                    .resource = resource,
                };
            }
        }
        for (const RenderGraphResourceId resource : pass.writes) {
            if (!resource.IsValid()) {
                return RenderPassGraphValidationResult{
                    .status = RenderPassGraphValidationStatus::InvalidResource,
                    .pass = pass.kind,
                    .resource = resource,
                };
            }
            if (FindResource(resource) == nullptr) {
                return RenderPassGraphValidationResult{
                    .status = RenderPassGraphValidationStatus::MissingResourceDeclaration,
                    .pass = pass.kind,
                    .resource = resource,
                };
            }
        }
    }

    std::array<bool, RenderGraphResource::Max> producedResources{};
    for (const RenderGraphResourceDesc& resource : resources_) {
        if (resource.id.value < producedResources.size() && resource.lifetime == RenderGraphResourceLifetime::External) {
            producedResources[resource.id.value] = true;
        }
    }
    for (const RenderPassDesc& pass : passes_) {
        if (!pass.enabled) {
            continue;
        }
        for (const RenderGraphResourceId resource : pass.reads) {
            if (resource.value >= producedResources.size() || !producedResources[resource.value]) {
                return RenderPassGraphValidationResult{
                    .status = RenderPassGraphValidationStatus::ReadBeforeWrite,
                    .pass = pass.kind,
                    .resource = resource,
                };
            }
        }
        for (const RenderGraphResourceId resource : pass.writes) {
            if (resource.value < producedResources.size()) {
                producedResources[resource.value] = true;
            }
        }
    }

    for (const RenderPassKind requiredPass : RequiredRenderPassKinds()) {
        const RenderPassDesc* pass = FindPass(requiredPass);
        if (pass == nullptr) {
            return RenderPassGraphValidationResult{
                .status = RenderPassGraphValidationStatus::MissingRequiredPass,
                .pass = requiredPass,
            };
        }
        if (!pass->enabled) {
            return RenderPassGraphValidationResult{
                .status = RenderPassGraphValidationStatus::DisabledRequiredPass,
                .pass = requiredPass,
            };
        }
    }

    return RenderPassGraphValidationResult{
        .status = RenderPassGraphValidationStatus::Success,
    };
}

RenderPassGraphCompileResult RenderPassGraph::Compile() const {
    RenderPassGraphCompileResult result{};
    result.validation = ValidateRequiredPasses();
    if (!result.validation.Succeeded()) {
        return result;
    }

    result.resourceUsages.reserve(resources_.size());
    for (const RenderGraphResourceDesc& resource : resources_) {
        result.transientResourceCount += resource.lifetime == RenderGraphResourceLifetime::Transient ? 1U : 0U;
        result.externalResourceCount += resource.lifetime == RenderGraphResourceLifetime::External ? 1U : 0U;
        if (resource.lifetime == RenderGraphResourceLifetime::Transient) {
            result.estimatedTransientBytes += ResourceBytes(resource);
        }

        RenderGraphResourceUsage usage{
            .resource = resource.id,
        };
        for (std::size_t passIndex = 0; passIndex < passes_.size(); ++passIndex) {
            const RenderPassDesc& pass = passes_[passIndex];
            if (!pass.enabled) {
                continue;
            }
            const auto markUse = [&usage, passIndex]() {
                const auto index = static_cast<std::uint16_t>(passIndex);
                usage.firstPassIndex = usage.firstPassIndex == 0xFFFFU ? index : std::min(usage.firstPassIndex, index);
                usage.lastPassIndex = std::max(usage.lastPassIndex, index);
            };
            for (const RenderGraphResourceId read : pass.reads) {
                if (read == resource.id) {
                    ++usage.readCount;
                    markUse();
                }
            }
            for (const RenderGraphResourceId write : pass.writes) {
                if (write == resource.id) {
                    ++usage.writeCount;
                    markUse();
                }
            }
        }
        result.resourceUsages.push_back(usage);
    }

    result.passProfiles.reserve(passes_.size());
    for (const RenderPassDesc& pass : passes_) {
        if (!pass.enabled) {
            continue;
        }
        RenderGraphPassProfile profile{
            .pass = pass.kind,
            .viewId = pass.viewId,
            .readCount = static_cast<std::uint16_t>(std::min<std::size_t>(pass.reads.size(), UINT16_MAX)),
            .writeCount = static_cast<std::uint16_t>(std::min<std::size_t>(pass.writes.size(), UINT16_MAX)),
            .emitsBgfxView = pass.emitsBgfxView,
        };
        for (const RenderGraphResourceId write : pass.writes) {
            if (const RenderGraphResourceDesc* resource = FindResource(write); resource != nullptr) {
                profile.estimatedTargetBytes += ResourceBytes(*resource);
            }
        }
        result.passProfiles.push_back(profile);
    }

    std::array<RenderGraphResourceAccess, RenderGraphResource::Max> lastAccess{};
    std::array<RenderPassKind, RenderGraphResource::Max> lastPass{};
    for (const RenderGraphResourceDesc& resource : resources_) {
        if (resource.id.value < lastAccess.size() && resource.lifetime == RenderGraphResourceLifetime::External) {
            lastAccess[resource.id.value] = ReadAccessFor(resource);
        }
    }
    for (const RenderPassDesc& pass : passes_) {
        if (!pass.enabled) {
            continue;
        }
        const auto transition = [&result, &lastAccess, &lastPass, this, &pass](RenderGraphResourceId resourceId, RenderGraphResourceAccess afterAccess) {
            if (resourceId.value >= lastAccess.size()) {
                return;
            }
            const RenderGraphResourceAccess beforeAccess = lastAccess[resourceId.value];
            if (AccessNeedsBarrier(beforeAccess, afterAccess)) {
                result.barriers.push_back(RenderGraphResourceBarrier{
                    .resource = resourceId,
                    .beforePass = lastPass[resourceId.value],
                    .afterPass = pass.kind,
                    .beforeAccess = beforeAccess,
                    .afterAccess = afterAccess,
                });
            }
            lastAccess[resourceId.value] = afterAccess;
            lastPass[resourceId.value] = pass.kind;
        };
        for (const RenderGraphResourceId read : pass.reads) {
            if (const RenderGraphResourceDesc* resource = FindResource(read); resource != nullptr) {
                transition(read, ReadAccessFor(*resource));
            }
        }
        for (const RenderGraphResourceId write : pass.writes) {
            if (const RenderGraphResourceDesc* resource = FindResource(write); resource != nullptr) {
                transition(write, WriteAccessFor(*resource));
            }
        }
    }

    for (std::size_t passIndex = 0; passIndex < passes_.size(); ++passIndex) {
        std::uint64_t activeTransientBytes = 0;
        for (std::size_t resourceIndex = 0; resourceIndex < resources_.size(); ++resourceIndex) {
            const RenderGraphResourceDesc& resource = resources_[resourceIndex];
            const RenderGraphResourceUsage& usage = result.resourceUsages[resourceIndex];
            if (resource.lifetime != RenderGraphResourceLifetime::Transient || usage.firstPassIndex == 0xFFFFU) {
                continue;
            }
            const std::uint16_t activePassIndex = static_cast<std::uint16_t>(passIndex);
            if (usage.firstPassIndex <= activePassIndex && activePassIndex <= usage.lastPassIndex) {
                activeTransientBytes += ResourceBytes(resource);
            }
        }
        result.estimatedAliasedTransientBytes = std::max(result.estimatedAliasedTransientBytes, activeTransientBytes);
    }
    result.transientAliasingSavingsBytes =
        result.estimatedTransientBytes > result.estimatedAliasedTransientBytes
            ? result.estimatedTransientBytes - result.estimatedAliasedTransientBytes
            : 0U;

    std::vector<std::uint16_t> aliasLastPass;
    result.aliases.reserve(resources_.size());
    for (std::size_t resourceIndex = 0; resourceIndex < resources_.size(); ++resourceIndex) {
        const RenderGraphResourceDesc& resource = resources_[resourceIndex];
        const RenderGraphResourceUsage& usage = result.resourceUsages[resourceIndex];
        if (resource.lifetime != RenderGraphResourceLifetime::Transient || usage.firstPassIndex == 0xFFFFU) {
            continue;
        }

        std::uint16_t aliasSlot = 0xFFFFU;
        for (std::uint16_t slot = 0U; slot < aliasLastPass.size(); ++slot) {
            if (aliasLastPass[slot] < usage.firstPassIndex) {
                aliasSlot = slot;
                aliasLastPass[slot] = usage.lastPassIndex;
                break;
            }
        }
        if (aliasSlot == 0xFFFFU) {
            aliasSlot = static_cast<std::uint16_t>(aliasLastPass.size());
            aliasLastPass.push_back(usage.lastPassIndex);
        }
        result.aliases.push_back(RenderGraphResourceAlias{
            .resource = resource.id,
            .aliasSlot = aliasSlot,
            .byteSize = ResourceBytes(resource),
        });
    }

    return result;
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
