#include "engine/render/postfx/PostProcessPipeline.hpp"

#include "render/postfx/PostProcessPipelineCompiler.hpp"
#include "render/postfx/PostProcessPipelineExecutor.hpp"
#include "render/postfx/PostProcessPipelineState.hpp"

#include <utility>

namespace kb::render::postfx {

struct PostProcessPipeline::Impl {
    PostProcessPipelineState state;
};

PostProcessPipeline::PostProcessPipeline()
    : impl_(std::make_unique<Impl>()) {
}

PostProcessPipeline::~PostProcessPipeline() = default;

PostProcessPipeline::PostProcessPipeline(const PostProcessPipeline& other)
    : impl_(std::make_unique<Impl>(*other.impl_)) {
}

PostProcessPipeline& PostProcessPipeline::operator=(const PostProcessPipeline& other) {
    if (this != &other) {
        impl_ = std::make_unique<Impl>(*other.impl_);
    }
    return *this;
}

PostProcessPipeline::PostProcessPipeline(PostProcessPipeline&& other) noexcept
    : impl_(std::exchange(other.impl_, std::make_unique<Impl>())) {
}

PostProcessPipeline& PostProcessPipeline::operator=(PostProcessPipeline&& other) noexcept {
    if (this != &other) {
        impl_ = std::exchange(other.impl_, std::make_unique<Impl>());
    }
    return *this;
}

ResourceHandle PostProcessPipeline::RegisterResource(ResourceDesc desc) {
    ResourceHandle handle = impl_->state.resources.Register(std::move(desc));
    impl_->state.compiled = false;
    return handle;
}

void PostProcessPipeline::AddPass(PassDesc desc) {
    impl_->state.validator.Validate(desc, impl_->state.resources.Resources());
    impl_->state.passes.push_back(std::move(desc));
    impl_->state.compiled = false;
}

void PostProcessPipeline::Compile() {
    PostProcessPipelineCompiler::Compile(impl_->state);
}

void PostProcessPipeline::Execute() {
    PostProcessPipelineExecutor::Execute(impl_->state);
}

const std::vector<std::uint32_t>& PostProcessPipeline::GetExecutionOrder() const noexcept {
    return impl_->state.graph.executionOrder;
}

} // namespace kb::render::postfx
