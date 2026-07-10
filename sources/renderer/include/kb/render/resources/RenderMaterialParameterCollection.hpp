#pragma once

#include "engine/assets/IAssetLoader.hpp"
#include "kb/render/resources/RenderMaterialAssetLoader.hpp"

#include <array>
#include <cstdint>
#include <filesystem>
#include <iosfwd>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <typeindex>
#include <vector>

namespace kb::render {

inline constexpr std::uint32_t kRenderMaterialParameterCollectionDocumentVersion = 1U;
inline constexpr const char* kRenderMaterialParameterCollectionAssetExtension = ".kbmpc";
inline constexpr const char* kRenderMaterialParameterCollectionAssetType = "RenderMaterialParameterCollection";

enum class RenderMaterialParameterCollectionValueType : std::uint8_t {
    Scalar,
    Vector,
};

struct RenderMaterialParameterCollectionParameter {
    std::string stableId;
    std::string displayName;
    RenderMaterialParameterCollectionValueType type = RenderMaterialParameterCollectionValueType::Scalar;
    std::array<float, 4U> defaultValue{};
    std::uint32_t editorOrder = 0U;
    std::string description;
};

struct RenderMaterialParameterCollectionData {
    std::uint32_t documentVersion = kRenderMaterialParameterCollectionDocumentVersion;
    bool hasExplicitDocumentVersion = false;
    std::string displayName;
    std::vector<RenderMaterialParameterCollectionParameter> parameters;
};

struct RenderMaterialParameterCollectionParseResult {
    std::optional<RenderMaterialParameterCollectionData> collection;
    std::vector<RenderMaterialAssetParseDiagnostic> diagnostics;

    [[nodiscard]] bool Succeeded() const noexcept;
    [[nodiscard]] std::string ErrorMessage() const;
};

class RenderMaterialParameterCollectionAssetLoader final : public kb::assets::IAssetLoader {
public:
    [[nodiscard]] std::string_view Type() const noexcept override;
    [[nodiscard]] std::type_index PayloadType() const noexcept override;
    [[nodiscard]] std::vector<std::string> Extensions() const override;
    [[nodiscard]] kb::assets::AssetLoadResult Load(const kb::assets::AssetLoadRequest& request) override;

    [[nodiscard]] static std::optional<RenderMaterialParameterCollectionData> LoadCollection(const std::filesystem::path& path);
    [[nodiscard]] static std::optional<RenderMaterialParameterCollectionData> LoadCollection(std::istream& input);
    [[nodiscard]] static RenderMaterialParameterCollectionParseResult LoadCollectionWithDiagnostics(const std::filesystem::path& path);
    [[nodiscard]] static RenderMaterialParameterCollectionParseResult LoadCollectionWithDiagnostics(const std::filesystem::path& path, kb::assets::AssetId assetId);
    [[nodiscard]] static RenderMaterialParameterCollectionParseResult LoadCollectionWithDiagnostics(std::istream& input);
};

class RenderMaterialParameterCollectionWriter final {
public:
    RenderMaterialParameterCollectionWriter() = delete;

    [[nodiscard]] static bool Save(const std::filesystem::path& path, const RenderMaterialParameterCollectionData& collection);
    static void Write(std::ostream& output, const RenderMaterialParameterCollectionData& collection);
};

struct RenderMaterialParameterCollectionRuntimeValue {
    std::uint64_t collectionAssetId = 0U;
    std::string stableId;
    RenderMaterialParameterCollectionValueType type = RenderMaterialParameterCollectionValueType::Scalar;
    std::array<float, 4U> value{};

    [[nodiscard]] friend bool operator==(const RenderMaterialParameterCollectionRuntimeValue&,
        const RenderMaterialParameterCollectionRuntimeValue&) noexcept = default;
};

class RenderMaterialParameterCollectionRuntimeStore {
public:
    void Clear() noexcept;
    [[nodiscard]] std::uint64_t Revision() const noexcept;
    [[nodiscard]] bool LoadDefaults(std::uint64_t collectionAssetId, const RenderMaterialParameterCollectionData& collection);
    [[nodiscard]] bool SetValue(
        std::uint64_t collectionAssetId,
        std::string_view stableId,
        RenderMaterialParameterCollectionValueType type,
        const std::array<float, 4U>& value);
    [[nodiscard]] bool ClearOverride(std::uint64_t collectionAssetId, std::string_view stableId);
    [[nodiscard]] bool UnloadCollection(std::uint64_t collectionAssetId);
    [[nodiscard]] bool RenameParameterStableId(
        std::uint64_t collectionAssetId,
        std::string_view oldStableId,
        std::string_view newStableId);
    [[nodiscard]] std::optional<RenderMaterialParameterCollectionRuntimeValue> Resolve(
        std::uint64_t collectionAssetId,
        std::string_view stableId) const;

private:
    std::vector<RenderMaterialParameterCollectionRuntimeValue> defaults_;
    std::vector<RenderMaterialParameterCollectionRuntimeValue> overrides_;
    std::uint64_t revision_ = 1U;
};

[[nodiscard]] RenderMaterialParameterCollectionRuntimeStore& GlobalRenderMaterialParameterCollectionStore() noexcept;
[[nodiscard]] std::vector<std::uint64_t> DiscoverRenderMaterialGraphParameterCollectionDependencies(const RenderMaterialGraphDocument& graph);
[[nodiscard]] std::uint64_t RenderMaterialGraphCollectionAssetId(const RenderMaterialGraphNode& node) noexcept;
[[nodiscard]] const RenderMaterialGraphNode* FindRenderMaterialGraphCollectionParameter(
    const RenderMaterialGraphDocument& graph,
    std::uint64_t collectionAssetId,
    std::string_view stableId) noexcept;

} // namespace kb::render
