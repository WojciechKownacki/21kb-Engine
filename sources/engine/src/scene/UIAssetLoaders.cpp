#include "engine/scene/UIAssetLoaders.hpp"

#include "engine/assets/AssetImportTypes.hpp"
#include "engine/assets/AssetRegistry.hpp"
#include "engine/scene/UIAssetIO.hpp"

#include <memory>
#include <optional>
#include <string>
#include <unordered_set>

namespace kb::scene {
namespace {

enum class UIDependencyKind : std::uint8_t {
    Style,
    Image,
    Font,
};

bool MatchesDependency(const kb::assets::AssetMetadata& metadata, UIDependencyKind kind) {
    switch (kind) {
    case UIDependencyKind::Style:
        return metadata.type == kUIStyleAssetType;
    case UIDependencyKind::Image:
        return metadata.type == "RenderTexture" || metadata.type == "Texture" ||
            (metadata.type == "ImportedAsset" &&
                metadata.importCategory == kb::assets::ToString(kb::assets::AssetImportCategory::Texture));
    case UIDependencyKind::Font:
        return metadata.type == "Font" ||
            (metadata.type == "ImportedAsset" &&
                metadata.importCategory == kb::assets::ToString(kb::assets::AssetImportCategory::Font));
    }
    return false;
}

std::string_view DependencyKindDescription(UIDependencyKind kind) {
    switch (kind) {
    case UIDependencyKind::Style: return "a UIStyle asset";
    case UIDependencyKind::Image: return "an image";
    case UIDependencyKind::Font: return "a font";
    }
    return "a UI dependency";
}

std::optional<std::string> ValidateDocumentDependencies(
    const UIDocument& document,
    const kb::assets::AssetRegistry& registry) {
    const auto validate = [&registry](std::uint64_t rawId, UIDependencyKind kind) -> std::optional<std::string> {
        if (rawId == 0U) return std::nullopt;
        const kb::assets::AssetMetadata* dependency = registry.Find(kb::assets::AssetId{ rawId });
        if (dependency == nullptr || MatchesDependency(*dependency, kind)) return std::nullopt;
        return "references asset " + std::to_string(rawId) + " as " +
            std::string{ DependencyKindDescription(kind) } + ", but its registered type is \"" +
            dependency->type + "\".";
    };

    if (const auto error = validate(document.styleAssetId, UIDependencyKind::Style)) return error;
    for (const UIDocumentElement& element : document.elements) {
        if (element.image) {
            if (const auto error = validate(element.image->imageAssetId, UIDependencyKind::Image)) return error;
        }
        if (element.textStyle) {
            if (const auto error = validate(element.textStyle->fontAssetId, UIDependencyKind::Font)) return error;
        }
    }
    return std::nullopt;
}

} // namespace

std::string_view UIDocumentAssetLoader::Type() const noexcept { return kUIDocumentAssetType; }
std::type_index UIDocumentAssetLoader::PayloadType() const noexcept { return typeid(UIDocument); }
std::vector<std::string> UIDocumentAssetLoader::Extensions() const { return { kUIDocumentAssetExtension }; }
kb::assets::AssetLoadResult UIDocumentAssetLoader::Load(const kb::assets::AssetLoadRequest& request) {
    std::vector<std::uint8_t> sourceBytes;
    std::string error;
    if (!request.ReadSourceBytes(sourceBytes, error)) {
        return kb::assets::AssetLoadResult{ {}, std::move(error) };
    }
    auto document = UIAssetIO::LoadDocument(sourceBytes);
    return document ? kb::assets::AssetLoadResult{ std::make_shared<UIDocument>(std::move(*document)), {} }
                    : kb::assets::AssetLoadResult{ {}, "UI document could not be loaded or parsed." };
}
std::vector<kb::assets::AssetId> UIDocumentAssetLoader::DiscoverDependencies(
    const kb::assets::AssetMetadata& metadata, const kb::assets::AssetRegistry& registry) const {
    static_cast<void>(registry);
    const auto document = UIAssetIO::LoadDocument(metadata.physicalPath);
    if (!document) return {};

    std::vector<kb::assets::AssetId> dependencies;
    std::unordered_set<std::uint64_t> seen;
    const auto add = [&dependencies, &seen](std::uint64_t rawId) {
        if (rawId == 0U || seen.contains(rawId)) return;
        static_cast<void>(seen.insert(rawId));
        dependencies.emplace_back(rawId);
    };

    add(document->styleAssetId);
    for (const UIDocumentElement& element : document->elements) {
        if (element.image) {
            add(element.image->imageAssetId);
        }
        if (element.textStyle) {
            add(element.textStyle->fontAssetId);
        }
    }
    return dependencies;
}

std::optional<std::string> UIDocumentAssetLoader::ValidateDependencies(
    const kb::assets::AssetMetadata& metadata,
    const kb::assets::AssetRegistry& registry) const {
    const auto document = UIAssetIO::LoadDocument(metadata.physicalPath);
    if (!document) return "could not be parsed while validating dependencies.";
    return ValidateDocumentDependencies(*document, registry);
}

std::optional<std::string> UIDocumentAssetLoader::ValidateRuntimeDependencies(
    const kb::assets::AssetLoadRequest& request,
    const kb::assets::AssetRegistry& registry) const {
    if (!request.IsPackaged()) return ValidateDependencies(request.metadata, registry);
    std::vector<std::uint8_t> sourceBytes;
    std::string error;
    if (!request.ReadSourceBytes(sourceBytes, error)) return error;
    const auto document = UIAssetIO::LoadDocument(sourceBytes);
    if (!document) return "could not be parsed while validating dependencies.";
    return ValidateDocumentDependencies(*document, registry);
}

std::string_view UIStyleAssetLoader::Type() const noexcept { return kUIStyleAssetType; }
std::type_index UIStyleAssetLoader::PayloadType() const noexcept { return typeid(UIStyleAsset); }
std::vector<std::string> UIStyleAssetLoader::Extensions() const { return { kUIStyleAssetExtension }; }
kb::assets::AssetLoadResult UIStyleAssetLoader::Load(const kb::assets::AssetLoadRequest& request) {
    std::vector<std::uint8_t> sourceBytes;
    std::string error;
    if (!request.ReadSourceBytes(sourceBytes, error)) {
        return kb::assets::AssetLoadResult{ {}, std::move(error) };
    }
    auto style = UIAssetIO::LoadStyle(sourceBytes);
    return style ? kb::assets::AssetLoadResult{ std::make_shared<UIStyleAsset>(std::move(*style)), {} }
                 : kb::assets::AssetLoadResult{ {}, "UI style asset could not be loaded or parsed." };
}

} // namespace kb::scene
