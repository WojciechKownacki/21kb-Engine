#include "kb/render/post/PostProcessChain.hpp"

#include <algorithm>
#include <cstddef>
#include <utility>

namespace kb::render {
namespace {

[[nodiscard]] bool ContainsPassKind(std::span<const PostProcessPass> passes, PostProcessPassKind kind) noexcept {
    return std::ranges::any_of(passes, [kind](const PostProcessPass& existing) {
        return existing.kind == kind;
    });
}

} // namespace

bool PostProcessInput::IsValid() const noexcept {
    return bgfx::isValid(sceneColor) && bgfx::isValid(outputFrameBuffer) && bgfx::isValid(outputColor) && extent.IsValid();
}

bool PostProcessOutput::IsValid() const noexcept {
    return bgfx::isValid(color) && extent.IsValid();
}

PostProcessChainDesc PostProcessChain::DefaultSceneChainDesc() {
    return PostProcessChainDesc{
        .passes = {
            kDefaultIdentityPass,
            PostProcessPass{.kind = PostProcessPassKind::AntiAliasing, .enabled = true},
            PostProcessPass{.kind = PostProcessPassKind::Bloom, .enabled = true},
            PostProcessPass{.kind = PostProcessPassKind::SelectionOutline, .enabled = true},
            PostProcessPass{
                .kind = PostProcessPassKind::Tonemap,
                .enabled = true,
                .outputTransform = SceneDisplayOutputTransform{
                    .autoExposure = FullscreenTextureAutoExposureSettings{
                        .enabled = true,
                    },
                },
            },
        },
    };
}

void PostProcessChain::Clear() noexcept {
    passes_.clear();
}

bool PostProcessChain::Configure(const PostProcessChainDesc& desc) {
    std::vector<PostProcessPass> configured;
    configured.reserve(desc.passes.size());
    for (const PostProcessPass& pass : desc.passes) {
        if (ContainsPassKind(configured, pass.kind)) {
            return false;
        }
        configured.push_back(pass);
    }

    passes_ = std::move(configured);
    return true;
}

bool PostProcessChain::AddPass(PostProcessPass pass) {
    if (ContainsPassKind(passes_, pass.kind)) {
        return false;
    }

    passes_.push_back(pass);
    return true;
}

bool PostProcessChain::InsertPass(std::uint32_t index, PostProcessPass pass) {
    if (index > passes_.size() || ContainsPassKind(passes_, pass.kind)) {
        return false;
    }

    passes_.insert(passes_.begin() + static_cast<std::ptrdiff_t>(index), pass);
    return true;
}

bool PostProcessChain::RemovePass(PostProcessPassKind kind) noexcept {
    const auto found = std::ranges::find_if(passes_, [kind](const PostProcessPass& existing) {
        return existing.kind == kind;
    });
    if (found == passes_.end()) {
        return false;
    }

    passes_.erase(found);
    return true;
}

bool PostProcessChain::SetPass(PostProcessPass pass) {
    const auto found = std::ranges::find_if(passes_, [pass](const PostProcessPass& existing) {
        return existing.kind == pass.kind;
    });
    if (found == passes_.end()) {
        return false;
    }

    *found = pass;
    return true;
}

bool PostProcessChain::SetPassEnabled(PostProcessPassKind kind, bool enabled) noexcept {
    const auto found = std::ranges::find_if(passes_, [kind](const PostProcessPass& existing) {
        return existing.kind == kind;
    });
    if (found == passes_.end()) {
        return false;
    }

    found->enabled = enabled;
    return true;
}

std::optional<PostProcessPass> PostProcessChain::FindPass(PostProcessPassKind kind) const noexcept {
    const auto found = std::ranges::find_if(passes_, [kind](const PostProcessPass& existing) {
        return existing.kind == kind;
    });
    if (found == passes_.end()) {
        return std::nullopt;
    }

    return *found;
}

std::span<const PostProcessPass> PostProcessChain::Passes() const noexcept {
    return passes_;
}

bool PostProcessChain::HasEnabledPasses() const noexcept {
    return std::ranges::any_of(passes_, [](const PostProcessPass& pass) {
        return pass.enabled;
    });
}

PostProcessOutput PostProcessChain::Evaluate(const PostProcessInput& input) const {
    if (!input.IsValid()) {
        return {};
    }

    PostProcessOutput output{
        .color = input.outputColor,
        .extent = input.extent,
        .producer = PostProcessPassKind::IdentityCopy,
        .colorSpace = PostProcessColorSpace::SceneHdr,
        .enabledPassCount = 0U,
        .passthrough = true,
        .gpuSubmitted = false,
        .sceneHdrPreserved = true,
    };

    for (const PostProcessPass& pass : passes_) {
        if (!pass.enabled) {
            continue;
        }

        ++output.enabledPassCount;
        output.producer = pass.kind;
        switch (pass.kind) {
        case PostProcessPassKind::IdentityCopy:
            output.colorSpace = PostProcessColorSpace::SceneHdr;
            output.sceneHdrPreserved = true;
            output.passthrough = true;
            break;
        case PostProcessPassKind::AntiAliasing:
            output.postProcessSettings = pass.postProcessSettings;
            output.fxaaEnabled = pass.postProcessSettings.fxaaEnabled;
            output.temporalAntiAliasingEnabled = pass.postProcessSettings.temporalAntiAliasingEnabled;
            output.colorSpace = PostProcessColorSpace::SceneHdr;
            output.sceneHdrPreserved = true;
            output.passthrough = true;
            break;
        case PostProcessPassKind::Bloom:
            output.postProcessSettings = pass.postProcessSettings;
            output.bloomEnabled = pass.postProcessSettings.bloomEnabled && pass.postProcessSettings.bloomStrength > 0.0F;
            output.colorSpace = PostProcessColorSpace::SceneHdr;
            output.sceneHdrPreserved = true;
            output.passthrough = true;
            break;
        case PostProcessPassKind::SelectionOutline:
            if (!bgfx::isValid(input.selectionMask)) {
                return {};
            }
            output.selectionOutlineEnabled = true;
            output.colorSpace = PostProcessColorSpace::SceneHdr;
            output.sceneHdrPreserved = true;
            output.passthrough = true;
            break;
        case PostProcessPassKind::Tonemap:
            output.postProcessSettings = pass.postProcessSettings;
            output.outputTransform = pass.outputTransform;
            output.tonemapEnabled = true;
            output.colorSpace = PostProcessColorSpace::SceneHdr;
            output.sceneHdrPreserved = true;
            output.passthrough = true;
            break;
        }
    }

    return output;
}

} // namespace kb::render
