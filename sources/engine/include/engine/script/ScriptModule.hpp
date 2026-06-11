#pragma once

#include "engine/modules/IEngineModule.hpp"
#include "engine/modules/EngineModuleMetadata.hpp"
#include "engine/script/ScriptRuntimeHost.hpp"

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace kb::script {

struct ScriptModuleOptions {
    ScriptRuntimeHostOptions runtimeOptions{};
    std::function<void(ScriptRuntimeHost&)> configureHost{};
};

// Built-in scripting module. It owns ScriptRuntimeHost and installs the script
// scene system during scene attach, while callers can inject editor/runtime
// specific API functions through configureHost without coupling engine code to
// those layers.
class ScriptModule final : public kb::modules::IEngineModule {
public:
    explicit ScriptModule(ScriptModuleOptions options = {});

    [[nodiscard]] kb::modules::EngineModuleMetadata Metadata() const override;
    void OnSceneAttach(kb::scene::Scene& scene) override;
    void OnUnload() override;

    [[nodiscard]] bool Succeeded() const noexcept;
    [[nodiscard]] const std::vector<std::string>& Diagnostics() const noexcept;
    [[nodiscard]] ScriptRuntimeHost* Host() noexcept;
    [[nodiscard]] const ScriptRuntimeHost* Host() const noexcept;

private:
    ScriptModuleOptions options_;
    std::unique_ptr<ScriptRuntimeHost> host_;
    std::vector<std::string> diagnostics_;
};

} // namespace kb::script
