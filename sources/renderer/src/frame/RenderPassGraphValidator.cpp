#include "frame/RenderPassGraphValidator.hpp"

#include <algorithm>

namespace kb::render {
namespace {

[[nodiscard]] const RenderGraphResourceDesc* FindResource(std::span<const RenderGraphResourceDesc> resources, RenderGraphResourceId resource) noexcept {
    const auto iter = std::ranges::find_if(resources, [resource](const RenderGraphResourceDesc& desc) {
        return desc.id == resource;
    });
    return iter == resources.end() ? nullptr : &(*iter);
}

[[nodiscard]] RenderPassGraphValidationResult ValidatePassDeclarations(
    std::span<const RenderGraphResourceDesc> resources,
    std::span<const RenderPassDesc> passes) noexcept {
    for (const RenderPassDesc& pass : passes) {
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
            if (FindResource(resources, resource) == nullptr) {
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
            if (FindResource(resources, resource) == nullptr) {
                return RenderPassGraphValidationResult{
                    .status = RenderPassGraphValidationStatus::MissingResourceDeclaration,
                    .pass = pass.kind,
                    .resource = resource,
                };
            }
        }
    }

    return RenderPassGraphValidationResult{
        .status = RenderPassGraphValidationStatus::Success,
    };
}

[[nodiscard]] RenderPassGraphValidationResult ValidateResourceProduction(
    std::span<const RenderGraphResourceDesc> resources,
    std::span<const RenderPassDesc> passes) noexcept {
    std::array<bool, RenderGraphResource::Max> producedResources{};
    for (const RenderGraphResourceDesc& resource : resources) {
        if (resource.id.value < producedResources.size() && resource.lifetime == RenderGraphResourceLifetime::External) {
            producedResources[resource.id.value] = true;
        }
    }
    for (const RenderPassDesc& pass : passes) {
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

    return RenderPassGraphValidationResult{
        .status = RenderPassGraphValidationStatus::Success,
    };
}

[[nodiscard]] RenderPassGraphValidationResult ValidateRequiredPasses(std::span<const RenderPassDesc> passes) noexcept {
    for (const RenderPassKind requiredPass : RequiredRenderPassKinds()) {
        const auto iter = std::ranges::find_if(passes, [requiredPass](const RenderPassDesc& pass) {
            return pass.kind == requiredPass;
        });
        if (iter == passes.end()) {
            return RenderPassGraphValidationResult{
                .status = RenderPassGraphValidationStatus::MissingRequiredPass,
                .pass = requiredPass,
            };
        }
        if (!iter->enabled) {
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

} // namespace

RenderPassGraphValidationResult RenderPassGraphValidator::Validate(
    std::span<const RenderGraphResourceDesc> resources,
    std::span<const RenderPassDesc> passes) noexcept {
    if (passes.empty()) {
        return RenderPassGraphValidationResult{
            .status = RenderPassGraphValidationStatus::EmptyGraph,
        };
    }

    RenderPassGraphValidationResult result = ValidatePassDeclarations(resources, passes);
    if (!result.Succeeded()) {
        return result;
    }

    result = ValidateResourceProduction(resources, passes);
    if (!result.Succeeded()) {
        return result;
    }

    return ValidateRequiredPasses(passes);
}

} // namespace kb::render
