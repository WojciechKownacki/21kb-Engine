#pragma once

#include <string_view>
#include <vector>

namespace kb::library {

// LIB-084: UI-facing metadata for one field of a cataloged component —
// deliberately separate from kb::script::ScriptSceneComponentPropertyDesc
// (name+ScriptValueType+writable, LIB-077) rather than adding these fields
// to it directly: ScriptSceneComponentPropertyDesc is a runtime SCHEMA
// contract (what type is this, can a script write it) consumed by the
// GetProperty/SetProperty dispatch path, while displayName/tooltip are pure
// presentation data with no bearing on that dispatch. `fieldName` is the
// same key ScriptSceneComponentPropertyDesc::name already uses (e.g.
// "localPosition.x"), so a consumer looks up UI metadata by the identical
// name it already has from ComponentProperties() — no second field-schema
// to keep in sync, just an optional presentation layer on top of the
// existing one.
struct LibraryComponentInspectorFieldDesc {
    std::string_view fieldName;
    std::string_view displayName;
    std::string_view tooltip;
};

// LIB-084: UI-facing metadata for one cataloged component — mirrors
// LibraryComponentDesc (LIB-076) the same way LibraryComponentInspectorFieldDesc
// mirrors ScriptSceneComponentPropertyDesc: `componentName` is the same key
// LibraryComponentDesc::name and ScriptSceneComponentApi::ComponentNames()
// already use, kept as a SEPARATE type (not new fields bolted onto
// LibraryComponentDesc) because inspector metadata is presentation-only and
// has no bearing on LibraryComponentDesc's runtime contract (version,
// threadPolicy, capability, serializable).
struct LibraryComponentInspectorDesc {
    std::string_view componentName;
    std::string_view displayName;
    // Presentation grouping, such as "Rendering" or "Scripting".
    std::string_view category;
    std::vector<LibraryComponentInspectorFieldDesc> fields;
};

// Engine-owned presentation metadata, independent of the editor. The editor's
// generic component inspector consumes FindField labels and tooltips.
// RunComponentInspectorDescCatalogTest cross-checks component and field coverage
// against ScriptSceneComponentApi, including compatibility aliases.
class EngineLibraryComponentInspectorRegistry final {
public:
    EngineLibraryComponentInspectorRegistry() = delete;

    [[nodiscard]] static const std::vector<LibraryComponentInspectorDesc>& Catalog();
    [[nodiscard]] static const LibraryComponentInspectorDesc* Find(std::string_view componentName) noexcept;
    [[nodiscard]] static const LibraryComponentInspectorFieldDesc* FindField(std::string_view componentName, std::string_view fieldName) noexcept;
};

} // namespace kb::library
