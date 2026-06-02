#include "kb/render/post/PostProcessChain.hpp"

#include <algorithm>

namespace kb::render {

bool PostProcessInput::IsValid() const noexcept {
    return bgfx::isValid(sceneColor) && bgfx::isValid(outputFrameBuffer) && bgfx::isValid(outputColor) && extent.IsValid();
}

bool PostProcessOutput::IsValid() const noexcept {
    return bgfx::isValid(color) && extent.IsValid();
}

void PostProcessChain::Clear() noexcept {
    passes_.clear();
}

bool PostProcessChain::AddPass(PostProcessPass pass) {
    const auto duplicateKind = [pass](const PostProcessPass& existing) {
        return existing.kind == pass.kind;
    };
    if (std::ranges::any_of(passes_, duplicateKind)) {
        return false;
    }

    passes_.push_back(pass);
    return true;
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
        case PostProcessPassKind::Bloom:
            output.colorSpace = PostProcessColorSpace::SceneHdr;
            output.sceneHdrPreserved = true;
            output.passthrough = true;
            break;
        case PostProcessPassKind::SelectionOutline:
            if (!bgfx::isValid(input.selectionMask)) {
                return {};
            }
            output.colorSpace = PostProcessColorSpace::SceneHdr;
            output.sceneHdrPreserved = true;
            output.passthrough = true;
            break;
        case PostProcessPassKind::Tonemap:
            output.colorSpace = PostProcessColorSpace::SceneHdr;
            output.sceneHdrPreserved = true;
            output.passthrough = true;
            break;
        }
    }

    return output;
}

} // namespace kb::render
