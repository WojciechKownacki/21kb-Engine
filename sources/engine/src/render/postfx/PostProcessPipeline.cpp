#include "engine/render/postfx/PostProcessPipeline.hpp"

#include "render/postfx/PostProcessCompiledPass.hpp"
#include "render/postfx/PostProcessDependencyGraph.hpp"
#include "render/postfx/PostProcessGraphBuilder.hpp"
#include "render/postfx/PostProcessPassCompiler.hpp"
#include "render/postfx/PostProcessPassValidator.hpp"
#include "render/postfx/PostProcessResourceRegistry.hpp"

#include <utility>

namespace kb::render::postfx {

struct PostProcessPipeline::Impl {
    PostProcessResourceRegistry resources;
    PostProcessPassValidator validator;
    PostProcessPassCompiler passCompiler;
    PostProcessGraphBuilder graphBuilder;
    std::vector<PassDesc> passes;
    std::vector<PostProcessCompiledPass> compiledPasses;
    PostProcessDependencyGraph graph;
    bool compiled = false;
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
    ResourceHandle handle = impl_->resources.Register(std::move(desc));
    impl_->compiled = false;
    return handle;
}

void PostProcessPipeline::AddPass(PassDesc desc) {
    impl_->validator.Validate(desc, impl_->resources.Resources());
    impl_->passes.push_back(std::move(desc));
    impl_->compiled = false;
}

void PostProcessPipeline::Compile() {
    if (impl_->passes.empty()) {
        throw std::runtime_error("PostProcess pipeline has no passes");
    }

    impl_->compiledPasses = impl_->passCompiler.Compile(impl_->passes);
    impl_->graph = impl_->graphBuilder.Build(impl_->compiledPasses);
    impl_->compiled = true;
}

void PostProcessPipeline::Execute() {
    if (!impl_->compiled) {
        Compile();
    }

    PassContext ctx{ .resources = &impl_->resources.Resources() };

    for (const auto passIndex : impl_->graph.executionOrder) {
        impl_->compiledPasses[passIndex].execute(ctx);
    }
}

const std::vector<std::uint32_t>& PostProcessPipeline::GetExecutionOrder() const noexcept {
    return impl_->graph.executionOrder;
}

} // namespace kb::render::postfx
