#include "engine/script/ScriptModule.hpp"

#include <utility>

namespace kb::script {

ScriptModule::ScriptModule(ScriptModuleOptions options)
    : options_(std::move(options)) {}

kb::modules::EngineModuleMetadata ScriptModule::Metadata() const {
    return kb::modules::EngineModuleMetadata{
        "Script",
        1U,
        {},
        kb::modules::EngineModuleLoadingPhase::Default,
    };
}

void ScriptModule::OnSceneAttach(kb::scene::Scene& scene) {
    if (host_ != nullptr) {
        return;
    }

    diagnostics_.clear();
    ScriptRuntimeHostOptions runtimeOptions = std::move(options_.runtimeOptions);
    // Do NOT install the scene system from the constructor: its startup pass
    // dispatches a lifecycle event that LOCKS the function registry (LIB-021),
    // after which no function can be registered. configureHost registers the
    // host's extra functions (e.g. the editor's Console-routing Log) and must
    // run BEFORE that lock, so install the scene system explicitly afterwards.
    runtimeOptions.installSceneSystem = false;
    host_ = std::make_unique<ScriptRuntimeHost>(scene, std::move(runtimeOptions));

    if (options_.configureHost) {
        options_.configureHost(*host_);
    }
    static_cast<void>(host_->InstallSceneSystem());

    diagnostics_ = host_->Diagnostics();
}

void ScriptModule::OnUnload() {
    host_.reset();
    diagnostics_.clear();
}

bool ScriptModule::Succeeded() const noexcept {
    return diagnostics_.empty();
}

const std::vector<std::string>& ScriptModule::Diagnostics() const noexcept {
    return diagnostics_;
}

ScriptRuntimeHost* ScriptModule::Host() noexcept {
    return host_.get();
}

const ScriptRuntimeHost* ScriptModule::Host() const noexcept {
    return host_.get();
}

} // namespace kb::script
