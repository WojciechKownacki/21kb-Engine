#include "engine/library/EngineLibraryManifestComparison.hpp"

#include <algorithm>
#include <cstddef>

namespace kb::library {

namespace {

using kb::script::ScriptApiCatalog;
using kb::script::ScriptApiCatalogComponent;
using kb::script::ScriptApiCatalogFunction;
using kb::script::ScriptApiCatalogProperty;
using kb::script::ScriptApiPin;

[[nodiscard]] bool PinEquals(const ScriptApiPin& lhs, const ScriptApiPin& rhs) noexcept {
    return lhs.name == rhs.name && lhs.type == rhs.type && lhs.required == rhs.required;
}

[[nodiscard]] bool PinListEquals(const std::vector<ScriptApiPin>& lhs, const std::vector<ScriptApiPin>& rhs) noexcept {
    if (lhs.size() != rhs.size()) {
        return false;
    }
    for (std::size_t index = 0; index < lhs.size(); ++index) {
        if (!PinEquals(lhs[index], rhs[index])) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool InputContractIsBackwardCompatible(
    const std::vector<ScriptApiPin>& baseline,
    const std::vector<ScriptApiPin>& current) noexcept {
    if (current.size() < baseline.size()) {
        return false;
    }
    for (std::size_t index = 0; index < baseline.size(); ++index) {
        const ScriptApiPin& oldPin = baseline[index];
        const ScriptApiPin& newPin = current[index];
        if (oldPin.name != newPin.name || oldPin.type != newPin.type) {
            return false;
        }
        if (!oldPin.required && newPin.required) {
            return false;
        }
    }
    return std::ranges::all_of(
        current.begin() + static_cast<std::ptrdiff_t>(baseline.size()),
        current.end(),
        [](const ScriptApiPin& pin) {
            return !pin.required;
        });
}

void CompareFunctions(const ScriptApiCatalog& baseline, const ScriptApiCatalog& current, std::vector<ApiChange>& changes) {
    for (const ScriptApiCatalogFunction& baselineFunction : baseline.functions) {
        const ScriptApiCatalogFunction* currentFunction = current.FindFunction(baselineFunction.name);
        if (currentFunction == nullptr) {
            changes.push_back(ApiChange{ ApiChangeSeverity::Breaking, "function '" + baselineFunction.name + "' was removed" });
            continue;
        }
        if (!InputContractIsBackwardCompatible(baselineFunction.inputs, currentFunction->inputs)) {
            changes.push_back(ApiChange{ ApiChangeSeverity::Breaking, "function '" + baselineFunction.name + "' input contract changed" });
        } else if (!PinListEquals(baselineFunction.inputs, currentFunction->inputs)) {
            changes.push_back(ApiChange{
                ApiChangeSeverity::Additive,
                "function '" + baselineFunction.name + "' input contract was extended compatibly",
            });
        }
        if (!PinListEquals(baselineFunction.outputs, currentFunction->outputs)) {
            changes.push_back(ApiChange{ ApiChangeSeverity::Breaking, "function '" + baselineFunction.name + "' output contract changed" });
        }
    }
    for (const ScriptApiCatalogFunction& currentFunction : current.functions) {
        if (baseline.FindFunction(currentFunction.name) == nullptr) {
            changes.push_back(ApiChange{ ApiChangeSeverity::Additive, "function '" + currentFunction.name + "' was added" });
        }
    }
}

[[nodiscard]] const ScriptApiCatalogComponent* FindComponent(const ScriptApiCatalog& catalog, const std::string& name) noexcept {
    const auto iter = std::ranges::find_if(catalog.components, [&name](const ScriptApiCatalogComponent& component) {
        return component.name == name;
    });
    return iter == catalog.components.end() ? nullptr : &*iter;
}

[[nodiscard]] const ScriptApiCatalogProperty* FindProperty(const ScriptApiCatalogComponent& component, const std::string& name) noexcept {
    const auto iter = std::ranges::find_if(component.properties, [&name](const ScriptApiCatalogProperty& property) {
        return property.name == name;
    });
    return iter == component.properties.end() ? nullptr : &*iter;
}

void CompareComponents(const ScriptApiCatalog& baseline, const ScriptApiCatalog& current, std::vector<ApiChange>& changes) {
    for (const ScriptApiCatalogComponent& baselineComponent : baseline.components) {
        const ScriptApiCatalogComponent* currentComponent = FindComponent(current, baselineComponent.name);
        if (currentComponent == nullptr) {
            changes.push_back(ApiChange{ ApiChangeSeverity::Breaking, "component '" + baselineComponent.name + "' was removed" });
            continue;
        }
        for (const ScriptApiCatalogProperty& baselineProperty : baselineComponent.properties) {
            const ScriptApiCatalogProperty* currentProperty = FindProperty(*currentComponent, baselineProperty.name);
            const std::string propertyLabel = baselineComponent.name + "." + baselineProperty.name;
            if (currentProperty == nullptr) {
                changes.push_back(ApiChange{ ApiChangeSeverity::Breaking, "property '" + propertyLabel + "' was removed" });
                continue;
            }
            if (currentProperty->type != baselineProperty.type) {
                changes.push_back(ApiChange{ ApiChangeSeverity::Breaking, "property '" + propertyLabel + "' type changed" });
            }
            if (baselineProperty.writable && !currentProperty->writable) {
                changes.push_back(ApiChange{ ApiChangeSeverity::Breaking, "property '" + propertyLabel + "' became read-only" });
            }
        }
        for (const ScriptApiCatalogProperty& currentProperty : currentComponent->properties) {
            if (FindProperty(baselineComponent, currentProperty.name) == nullptr) {
                changes.push_back(ApiChange{ ApiChangeSeverity::Additive, "property '" + baselineComponent.name + "." + currentProperty.name + "' was added" });
            }
        }
    }
    for (const ScriptApiCatalogComponent& currentComponent : current.components) {
        if (FindComponent(baseline, currentComponent.name) == nullptr) {
            changes.push_back(ApiChange{ ApiChangeSeverity::Additive, "component '" + currentComponent.name + "' was added" });
        }
    }
}

void CompareLifecycleEvents(const ScriptApiCatalog& baseline, const ScriptApiCatalog& current, std::vector<ApiChange>& changes) {
    for (std::size_t index = 0; index < baseline.lifecycleEvents.size(); ++index) {
        if (index >= current.lifecycleEvents.size() || current.lifecycleEvents[index] != baseline.lifecycleEvents[index]) {
            changes.push_back(ApiChange{ ApiChangeSeverity::Breaking, "lifecycle event order/content changed at position " + std::to_string(index) });
            return;
        }
    }
    for (std::size_t index = baseline.lifecycleEvents.size(); index < current.lifecycleEvents.size(); ++index) {
        changes.push_back(ApiChange{ ApiChangeSeverity::Additive, "lifecycle event '" + current.lifecycleEvents[index] + "' was added" });
    }
}

} // namespace

bool ApiCompatibilityReport::HasBreakingChanges() const noexcept {
    return std::ranges::any_of(changes, [](const ApiChange& change) {
        return change.severity == ApiChangeSeverity::Breaking;
    });
}

ApiCompatibilityReport CompareApiCatalogs(const kb::script::ScriptApiCatalog& baseline, const kb::script::ScriptApiCatalog& current) {
    ApiCompatibilityReport report;
    CompareLifecycleEvents(baseline, current, report.changes);
    CompareFunctions(baseline, current, report.changes);
    CompareComponents(baseline, current, report.changes);
    return report;
}

} // namespace kb::library
