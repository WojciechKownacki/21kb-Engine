#include "frame/RenderPassGraphCompiler.hpp"

#include <algorithm>

namespace kb::render {
namespace {

[[nodiscard]] constexpr std::uint32_t BytesPerPixel(RenderTargetFormat format) noexcept {
    switch (format) {
    case RenderTargetFormat::Backbuffer:
    case RenderTargetFormat::Bgra8:
    case RenderTargetFormat::Rgba8:
    case RenderTargetFormat::D32:
    case RenderTargetFormat::D32F:
    case RenderTargetFormat::Rg16F:
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

[[nodiscard]] const RenderGraphResourceDesc* FindResource(std::span<const RenderGraphResourceDesc> resources, RenderGraphResourceId resource) noexcept {
    const auto iter = std::ranges::find_if(resources, [resource](const RenderGraphResourceDesc& desc) {
        return desc.id == resource;
    });
    return iter == resources.end() ? nullptr : &(*iter);
}

void BuildResourceUsages(
    std::span<const RenderGraphResourceDesc> resources,
    std::span<const RenderPassDesc> passes,
    RenderPassGraphCompileResult& result) {
    result.resourceUsages.reserve(resources.size());
    for (const RenderGraphResourceDesc& resource : resources) {
        result.transientResourceCount += resource.lifetime == RenderGraphResourceLifetime::Transient ? 1U : 0U;
        result.externalResourceCount += resource.lifetime == RenderGraphResourceLifetime::External ? 1U : 0U;
        if (resource.lifetime == RenderGraphResourceLifetime::Transient) {
            result.estimatedTransientBytes += ResourceBytes(resource);
        }

        RenderGraphResourceUsage usage{
            .resource = resource.id,
        };
        for (std::size_t passIndex = 0; passIndex < passes.size(); ++passIndex) {
            const RenderPassDesc& pass = passes[passIndex];
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
}

void BuildPassProfiles(
    std::span<const RenderGraphResourceDesc> resources,
    std::span<const RenderPassDesc> passes,
    RenderPassGraphCompileResult& result) {
    result.passProfiles.reserve(passes.size());
    for (const RenderPassDesc& pass : passes) {
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
            if (const RenderGraphResourceDesc* resource = FindResource(resources, write); resource != nullptr) {
                profile.estimatedTargetBytes += ResourceBytes(*resource);
            }
        }
        result.passProfiles.push_back(profile);
    }
}

void BuildBarriers(
    std::span<const RenderGraphResourceDesc> resources,
    std::span<const RenderPassDesc> passes,
    RenderPassGraphCompileResult& result) {
    std::array<RenderGraphResourceAccess, RenderGraphResource::Max> lastAccess{};
    std::array<RenderPassKind, RenderGraphResource::Max> lastPass{};
    for (const RenderGraphResourceDesc& resource : resources) {
        if (resource.id.value < lastAccess.size() && resource.lifetime == RenderGraphResourceLifetime::External) {
            lastAccess[resource.id.value] = ReadAccessFor(resource);
        }
    }
    for (const RenderPassDesc& pass : passes) {
        if (!pass.enabled) {
            continue;
        }
        const auto transition = [&result, &lastAccess, &lastPass, &pass](RenderGraphResourceId resourceId, RenderGraphResourceAccess afterAccess) {
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
            if (const RenderGraphResourceDesc* resource = FindResource(resources, read); resource != nullptr) {
                transition(read, ReadAccessFor(*resource));
            }
        }
        for (const RenderGraphResourceId write : pass.writes) {
            if (const RenderGraphResourceDesc* resource = FindResource(resources, write); resource != nullptr) {
                transition(write, WriteAccessFor(*resource));
            }
        }
    }
}

void EstimateAliasedTransientBytes(
    std::span<const RenderGraphResourceDesc> resources,
    RenderPassGraphCompileResult& result) {
    for (std::size_t passIndex = 0; passIndex < result.passProfiles.size(); ++passIndex) {
        std::uint64_t activeTransientBytes = 0;
        for (std::size_t resourceIndex = 0; resourceIndex < resources.size(); ++resourceIndex) {
            const RenderGraphResourceDesc& resource = resources[resourceIndex];
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
}

void BuildAliases(std::span<const RenderGraphResourceDesc> resources, RenderPassGraphCompileResult& result) {
    std::vector<std::uint16_t> aliasLastPass;
    result.aliases.reserve(resources.size());
    for (std::size_t resourceIndex = 0; resourceIndex < resources.size(); ++resourceIndex) {
        const RenderGraphResourceDesc& resource = resources[resourceIndex];
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
}

} // namespace

RenderPassGraphCompileResult RenderPassGraphCompiler::Compile(
    std::span<const RenderGraphResourceDesc> resources,
    std::span<const RenderPassDesc> passes,
    RenderPassGraphValidationResult validation) {
    RenderPassGraphCompileResult result{};
    result.validation = validation;
    if (!result.validation.Succeeded()) {
        return result;
    }

    BuildResourceUsages(resources, passes, result);
    BuildPassProfiles(resources, passes, result);
    BuildBarriers(resources, passes, result);
    EstimateAliasedTransientBytes(resources, result);
    BuildAliases(resources, result);
    return result;
}

} // namespace kb::render
