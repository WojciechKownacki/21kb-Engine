#pragma once

#include "engine/visual/VisualGraphNativeBindingRegistry.hpp"
#include "engine/visual/VisualGraphCompiler.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace kb::visual {

struct VisualGraphNativeCodegenDesc {
    std::string className;
    std::string namespaceName = "kb::generated";
    std::vector<std::string> sourceIncludes;
    const VisualGraphNativeBindingRegistry* bindings = nullptr;
    bool requireNativeBindings = false;
};

struct VisualGraphNativeCode {
    std::string header;
    std::string source;
    std::string headerFileName;
    std::string sourceFileName;
    std::vector<std::string> errors;
    std::vector<VisualGraphDiagnostic> diagnostics;

    [[nodiscard]] bool Succeeded() const noexcept {
        return errors.empty() && !VisualGraphDiagnostics::HasErrors(diagnostics);
    }
};

class VisualGraphNativeCodeGenerator {
public:
    VisualGraphNativeCodeGenerator() = delete;

    [[nodiscard]] static VisualGraphNativeCode Generate(const VisualGraphIrModule& module, const VisualGraphNativeCodegenDesc& desc);
};

} // namespace kb::visual
