#include "kb/render/resources/RenderMaterialParameterCollection.hpp"

#include "engine/assets/AssetId.hpp"
#include "engine/assets/AssetMemoryInputStream.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <memory>
#include <ostream>
#include <sstream>
#include <system_error>
#include <utility>

namespace kb::render {
namespace {

[[nodiscard]] std::string_view Trim(std::string_view text) noexcept {
    while (!text.empty() && (text.front() == ' ' || text.front() == '\t' || text.front() == '\r')) {
        text.remove_prefix(1U);
    }
    while (!text.empty() && (text.back() == ' ' || text.back() == '\t' || text.back() == '\r')) {
        text.remove_suffix(1U);
    }
    return text;
}

[[nodiscard]] std::string_view StripComment(std::string_view text) noexcept {
    const std::size_t comment = text.find('#');
    return comment == std::string_view::npos ? text : text.substr(0U, comment);
}

[[nodiscard]] bool EqualsIgnoreCase(std::string_view lhs, std::string_view rhs) noexcept {
    if (lhs.size() != rhs.size()) {
        return false;
    }
    for (std::size_t index = 0U; index < lhs.size(); ++index) {
        char left = lhs[index];
        char right = rhs[index];
        if (left >= 'A' && left <= 'Z') {
            left = static_cast<char>(left - 'A' + 'a');
        }
        if (right >= 'A' && right <= 'Z') {
            right = static_cast<char>(right - 'A' + 'a');
        }
        if (left != right) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool ParseUInt32(std::string_view text, std::uint32_t& output) noexcept {
    text = Trim(text);
    const char* first = text.data();
    const char* last = first + text.size();
    const std::from_chars_result result = std::from_chars(first, last, output);
    return result.ec == std::errc{} && result.ptr == last;
}

[[nodiscard]] bool ParseUInt64(std::string_view text, std::uint64_t& output) noexcept {
    text = Trim(text);
    const char* first = text.data();
    const char* last = first + text.size();
    const std::from_chars_result result = std::from_chars(first, last, output);
    return result.ec == std::errc{} && result.ptr == last;
}

[[nodiscard]] bool ParseFloat(std::string_view text, float& output) noexcept {
    text = Trim(text);
    const char* first = text.data();
    const char* last = first + text.size();
    const std::from_chars_result result = std::from_chars(first, last, output);
    return result.ec == std::errc{} && result.ptr == last && std::isfinite(output);
}

[[nodiscard]] bool ParseCollectionValueType(std::string_view text, RenderMaterialParameterCollectionValueType& output) noexcept {
    if (EqualsIgnoreCase(text, "Scalar") || EqualsIgnoreCase(text, "Float")) {
        output = RenderMaterialParameterCollectionValueType::Scalar;
        return true;
    }
    if (EqualsIgnoreCase(text, "Vector") || EqualsIgnoreCase(text, "Vec4") || EqualsIgnoreCase(text, "Color")) {
        output = RenderMaterialParameterCollectionValueType::Vector;
        return true;
    }
    return false;
}

[[nodiscard]] const char* CollectionValueTypeName(RenderMaterialParameterCollectionValueType type) noexcept {
    switch (type) {
    case RenderMaterialParameterCollectionValueType::Scalar:
        return "Scalar";
    case RenderMaterialParameterCollectionValueType::Vector:
        return "Vector";
    }
    return "Scalar";
}

[[nodiscard]] std::string DecodeToken(std::string_view value) {
    std::string decoded;
    for (std::size_t index = 0U; index < value.size(); ++index) {
        if (value[index] == '%' && index + 2U < value.size()) {
            const std::string_view code = value.substr(index + 1U, 2U);
            if (code == "20") {
                decoded += ' ';
                index += 2U;
                continue;
            }
            if (code == "09") {
                decoded += '\t';
                index += 2U;
                continue;
            }
            if (code == "0A") {
                decoded += '\n';
                index += 2U;
                continue;
            }
            if (code == "0D") {
                decoded += '\r';
                index += 2U;
                continue;
            }
            if (code == "25") {
                decoded += '%';
                index += 2U;
                continue;
            }
            if (code == "23") {
                decoded += '#';
                index += 2U;
                continue;
            }
        }
        decoded += value[index];
    }
    return decoded;
}

[[nodiscard]] std::string EncodeToken(std::string_view value) {
    if (value.empty()) {
        return "_";
    }
    std::string encoded;
    for (const char ch : value) {
        switch (ch) {
        case ' ': encoded += "%20"; break;
        case '\t': encoded += "%09"; break;
        case '\n': encoded += "%0A"; break;
        case '\r': encoded += "%0D"; break;
        case '%': encoded += "%25"; break;
        case '#': encoded += "%23"; break;
        default: encoded += ch; break;
        }
    }
    return encoded;
}

void AddDiagnostic(
    RenderMaterialParameterCollectionParseResult& result,
    RenderMaterialAssetParseDiagnosticCode code,
    std::size_t line,
    std::string field,
    std::string message,
    std::string text = {}) {
    result.diagnostics.push_back(RenderMaterialAssetParseDiagnostic{
        .code = code,
        .line = line,
        .field = std::move(field),
        .message = std::move(message),
        .text = std::move(text),
    });
}

[[nodiscard]] bool HasDuplicateStableId(
    const RenderMaterialParameterCollectionData& collection,
    std::string_view stableId) noexcept {
    return std::ranges::count_if(collection.parameters, [stableId](const RenderMaterialParameterCollectionParameter& parameter) {
        return parameter.stableId == stableId;
    }) > 1;
}

[[nodiscard]] bool ParseCollectionParameter(
    std::string_view rest,
    std::size_t line,
    RenderMaterialParameterCollectionData& collection,
    RenderMaterialParameterCollectionParseResult& result) {
    std::istringstream stream{ std::string{ rest } };
    std::string stableId;
    std::string typeText;
    std::string displayNameText;
    std::string xText;
    std::string yText;
    std::string zText;
    std::string wText;
    std::string editorOrderText;
    std::string descriptionText;
    if (!(stream >> stableId >> typeText >> displayNameText >> xText >> yText >> zText >> wText >> editorOrderText >> descriptionText)) {
        AddDiagnostic(
            result,
            RenderMaterialAssetParseDiagnosticCode::InvalidFieldValue,
            line,
            "collectionParameter",
            "Collection parameter requires stable id, type, display name, four defaults, editor order and description.",
            std::string{ rest });
        return false;
    }

    RenderMaterialParameterCollectionParameter parameter{};
    parameter.stableId = stableId;
    if (parameter.stableId.empty() || parameter.stableId == "_") {
        AddDiagnostic(result, RenderMaterialAssetParseDiagnosticCode::InvalidFieldValue, line, "collectionParameter", "Collection parameter stable id is required.", stableId);
        return false;
    }
    if (!ParseCollectionValueType(typeText, parameter.type) ||
        !ParseFloat(xText, parameter.defaultValue[0]) ||
        !ParseFloat(yText, parameter.defaultValue[1]) ||
        !ParseFloat(zText, parameter.defaultValue[2]) ||
        !ParseFloat(wText, parameter.defaultValue[3]) ||
        !ParseUInt32(editorOrderText, parameter.editorOrder)) {
        AddDiagnostic(result, RenderMaterialAssetParseDiagnosticCode::InvalidFieldValue, line, "collectionParameter", "Collection parameter value is invalid.", std::string{ rest });
        return false;
    }
    parameter.displayName = displayNameText == "_" ? parameter.stableId : DecodeToken(displayNameText);
    parameter.description = descriptionText == "_" ? std::string{} : DecodeToken(descriptionText);
    collection.parameters.push_back(std::move(parameter));
    if (HasDuplicateStableId(collection, stableId)) {
        AddDiagnostic(
            result,
            RenderMaterialAssetParseDiagnosticCode::InvalidFieldValue,
            line,
            "collectionParameter",
            "Collection parameter stable id '" + stableId + "' is duplicated.",
            stableId);
        return false;
    }
    return true;
}

[[nodiscard]] RenderMaterialParameterCollectionParseResult ParseCollection(std::istream& input) {
    RenderMaterialParameterCollectionParseResult result{};
    RenderMaterialParameterCollectionData collection{};
    bool sawContent = false;

    std::string line;
    std::size_t lineNumber = 0U;
    while (std::getline(input, line)) {
        ++lineNumber;
        const std::string_view trimmed = Trim(StripComment(line));
        if (trimmed.empty()) {
            continue;
        }
        sawContent = true;

        const std::size_t split = trimmed.find_first_of(" \t");
        const std::string_view keyword = split == std::string_view::npos ? trimmed : trimmed.substr(0U, split);
        const std::string_view rest = split == std::string_view::npos ? std::string_view{} : Trim(trimmed.substr(split + 1U));
        if (keyword == "version") {
            std::uint32_t version = 0U;
            if (!ParseUInt32(rest, version) || version == 0U) {
                AddDiagnostic(result, RenderMaterialAssetParseDiagnosticCode::InvalidDocumentVersion, lineNumber, "version", "Invalid collection document version.", std::string{ trimmed });
                continue;
            }
            if (version > kRenderMaterialParameterCollectionDocumentVersion) {
                AddDiagnostic(result, RenderMaterialAssetParseDiagnosticCode::UnsupportedDocumentVersion, lineNumber, "version", "Unsupported collection document version " + std::to_string(version) + ".", std::string{ trimmed });
                continue;
            }
            collection.documentVersion = version;
            collection.hasExplicitDocumentVersion = true;
            continue;
        }
        if (keyword == "displayName") {
            collection.displayName = rest == "_" ? std::string{} : DecodeToken(rest);
            continue;
        }
        if (keyword == "collectionParameter") {
            static_cast<void>(ParseCollectionParameter(rest, lineNumber, collection, result));
            continue;
        }
        AddDiagnostic(
            result,
            RenderMaterialAssetParseDiagnosticCode::UnknownField,
            lineNumber,
            std::string{ keyword },
            "Unknown material parameter collection field.",
            std::string{ trimmed });
    }

    if (!sawContent) {
        AddDiagnostic(result, RenderMaterialAssetParseDiagnosticCode::EmptyDocument, 0U, {}, "Material parameter collection asset is empty.");
    }
    if (collection.parameters.empty()) {
        AddDiagnostic(result, RenderMaterialAssetParseDiagnosticCode::InvalidFieldValue, 0U, "collectionParameter", "Material parameter collection requires at least one parameter.");
    }
    if (result.diagnostics.empty()) {
        result.collection = std::move(collection);
    }
    return result;
}

void WriteFloat(std::ostream& output, float value) {
    output << std::setprecision(9) << value;
}

} // namespace

bool RenderMaterialParameterCollectionParseResult::Succeeded() const noexcept {
    if (!collection.has_value()) {
        return false;
    }
    for (const RenderMaterialAssetParseDiagnostic& diagnostic : diagnostics) {
        if (diagnostic.severity == RenderMaterialAssetParseDiagnosticSeverity::Error) {
            return false;
        }
    }
    return true;
}

std::string RenderMaterialParameterCollectionParseResult::ErrorMessage() const {
    if (diagnostics.empty()) {
        return {};
    }
    const RenderMaterialAssetParseDiagnostic& diagnostic = diagnostics.front();
    std::string message = std::string{ RenderMaterialAssetParseDiagnosticCodeName(diagnostic.code) };
    if (diagnostic.line > 0U) {
        message += " line ";
        message += std::to_string(diagnostic.line);
    }
    if (!diagnostic.message.empty()) {
        message += ": ";
        message += diagnostic.message;
    }
    return message;
}

std::string_view RenderMaterialParameterCollectionAssetLoader::Type() const noexcept {
    return kRenderMaterialParameterCollectionAssetType;
}

std::type_index RenderMaterialParameterCollectionAssetLoader::PayloadType() const noexcept {
    return typeid(RenderMaterialParameterCollectionData);
}

std::vector<std::string> RenderMaterialParameterCollectionAssetLoader::Extensions() const {
    return { kRenderMaterialParameterCollectionAssetExtension };
}

kb::assets::AssetLoadResult RenderMaterialParameterCollectionAssetLoader::Load(const kb::assets::AssetLoadRequest& request) {
    std::vector<std::uint8_t> sourceBytes;
    std::string error;
    if (!request.ReadSourceBytes(sourceBytes, error)) {
        return kb::assets::AssetLoadResult{ .asset = {}, .error = std::move(error) };
    }
    kb::assets::AssetMemoryInputStream input{ sourceBytes };
    RenderMaterialParameterCollectionParseResult result = LoadCollectionWithDiagnostics(input);
    for (RenderMaterialAssetParseDiagnostic& diagnostic : result.diagnostics) {
        diagnostic.assetId = request.metadata.id;
        diagnostic.path = request.resolvedPath;
    }
    if (!result.collection.has_value()) {
        return kb::assets::AssetLoadResult{ .asset = {}, .error = result.ErrorMessage() };
    }
    return kb::assets::AssetLoadResult{
        .asset = std::make_shared<RenderMaterialParameterCollectionData>(*result.collection),
        .error = {},
    };
}

std::optional<RenderMaterialParameterCollectionData> RenderMaterialParameterCollectionAssetLoader::LoadCollection(const std::filesystem::path& path) {
    RenderMaterialParameterCollectionParseResult result = LoadCollectionWithDiagnostics(path);
    return result.collection;
}

std::optional<RenderMaterialParameterCollectionData> RenderMaterialParameterCollectionAssetLoader::LoadCollection(std::istream& input) {
    RenderMaterialParameterCollectionParseResult result = LoadCollectionWithDiagnostics(input);
    return result.collection;
}

RenderMaterialParameterCollectionParseResult RenderMaterialParameterCollectionAssetLoader::LoadCollectionWithDiagnostics(const std::filesystem::path& path) {
    return LoadCollectionWithDiagnostics(path, {});
}

RenderMaterialParameterCollectionParseResult RenderMaterialParameterCollectionAssetLoader::LoadCollectionWithDiagnostics(const std::filesystem::path& path, kb::assets::AssetId assetId) {
    std::ifstream input{ path, std::ios::binary };
    if (!input) {
        RenderMaterialParameterCollectionParseResult result{};
        result.diagnostics.push_back(RenderMaterialAssetParseDiagnostic{
            .code = RenderMaterialAssetParseDiagnosticCode::FileOpenFailed,
            .assetId = assetId,
            .path = path,
            .message = "Material parameter collection asset file could not be opened.",
        });
        return result;
    }

    RenderMaterialParameterCollectionParseResult result = ParseCollection(input);
    for (RenderMaterialAssetParseDiagnostic& diagnostic : result.diagnostics) {
        diagnostic.assetId = assetId;
        diagnostic.path = path;
    }
    return result;
}

RenderMaterialParameterCollectionParseResult RenderMaterialParameterCollectionAssetLoader::LoadCollectionWithDiagnostics(std::istream& input) {
    return ParseCollection(input);
}

bool RenderMaterialParameterCollectionWriter::Save(const std::filesystem::path& path, const RenderMaterialParameterCollectionData& collection) {
    std::error_code error;
    const std::filesystem::path parent = path.parent_path();
    if (!parent.empty()) {
        std::filesystem::create_directories(parent, error);
        if (error) {
            return false;
        }
    }

    std::ofstream output{ path, std::ios::binary | std::ios::trunc };
    if (!output) {
        return false;
    }
    Write(output, collection);
    return static_cast<bool>(output);
}

void RenderMaterialParameterCollectionWriter::Write(std::ostream& output, const RenderMaterialParameterCollectionData& collection) {
    output << "# KB material parameter collection\n";
    output << "version " << (collection.documentVersion == 0U ? kRenderMaterialParameterCollectionDocumentVersion : collection.documentVersion) << '\n';
    output << "displayName " << EncodeToken(collection.displayName) << '\n';
    for (const RenderMaterialParameterCollectionParameter& parameter : collection.parameters) {
        if (parameter.stableId.empty()) {
            continue;
        }
        output << "collectionParameter " << parameter.stableId << ' ' << CollectionValueTypeName(parameter.type) << ' '
               << EncodeToken(parameter.displayName) << ' ';
        WriteFloat(output, parameter.defaultValue[0]);
        output << ' ';
        WriteFloat(output, parameter.defaultValue[1]);
        output << ' ';
        WriteFloat(output, parameter.defaultValue[2]);
        output << ' ';
        WriteFloat(output, parameter.defaultValue[3]);
        output << ' ' << parameter.editorOrder << ' ' << EncodeToken(parameter.description) << '\n';
    }
}

void RenderMaterialParameterCollectionRuntimeStore::Clear() noexcept {
    defaults_.clear();
    overrides_.clear();
    ++revision_;
}

std::uint64_t RenderMaterialParameterCollectionRuntimeStore::Revision() const noexcept {
    return revision_;
}

bool RenderMaterialParameterCollectionRuntimeStore::LoadDefaults(
    std::uint64_t collectionAssetId,
    const RenderMaterialParameterCollectionData& collection) {
    if (collectionAssetId == 0U) {
        return false;
    }
    std::vector<RenderMaterialParameterCollectionRuntimeValue> refreshedDefaults;
    refreshedDefaults.reserve(collection.parameters.size());
    for (const RenderMaterialParameterCollectionParameter& parameter : collection.parameters) {
        if (parameter.stableId.empty()) {
            continue;
        }
        refreshedDefaults.push_back(RenderMaterialParameterCollectionRuntimeValue{
            .collectionAssetId = collectionAssetId,
            .stableId = parameter.stableId,
            .type = parameter.type,
            .value = parameter.defaultValue,
        });
    }

    std::vector<RenderMaterialParameterCollectionRuntimeValue> previousDefaults;
    for (const RenderMaterialParameterCollectionRuntimeValue& value : defaults_) {
        if (value.collectionAssetId == collectionAssetId) {
            previousDefaults.push_back(value);
        }
    }
    const bool defaultsChanged = previousDefaults != refreshedDefaults;
    std::erase_if(defaults_, [collectionAssetId](const RenderMaterialParameterCollectionRuntimeValue& value) {
        return value.collectionAssetId == collectionAssetId;
    });
    defaults_.insert(defaults_.end(), refreshedDefaults.begin(), refreshedDefaults.end());

    const std::size_t overrideCountBefore = overrides_.size();
    std::erase_if(overrides_, [collectionAssetId, &refreshedDefaults](const RenderMaterialParameterCollectionRuntimeValue& value) {
        if (value.collectionAssetId != collectionAssetId) {
            return false;
        }
        return std::ranges::none_of(refreshedDefaults, [&value](const RenderMaterialParameterCollectionRuntimeValue& defaultValue) {
            return defaultValue.stableId == value.stableId && defaultValue.type == value.type;
        });
    });
    const bool changed = defaultsChanged || overrides_.size() != overrideCountBefore;
    if (changed) {
        ++revision_;
    }
    return changed;
}

bool RenderMaterialParameterCollectionRuntimeStore::SetValue(
    std::uint64_t collectionAssetId,
    std::string_view stableId,
    RenderMaterialParameterCollectionValueType type,
    const std::array<float, 4U>& value) {
    if (collectionAssetId == 0U || stableId.empty()) {
        return false;
    }
    for (RenderMaterialParameterCollectionRuntimeValue& existing : overrides_) {
        if (existing.collectionAssetId == collectionAssetId && existing.stableId == stableId) {
            if (existing.type == type && existing.value == value) {
                return false;
            }
            existing.type = type;
            existing.value = value;
            ++revision_;
            return true;
        }
    }
    overrides_.push_back(RenderMaterialParameterCollectionRuntimeValue{
        .collectionAssetId = collectionAssetId,
        .stableId = std::string{ stableId },
        .type = type,
        .value = value,
    });
    ++revision_;
    return true;
}

bool RenderMaterialParameterCollectionRuntimeStore::ClearOverride(
    std::uint64_t collectionAssetId,
    std::string_view stableId) {
    const std::size_t before = overrides_.size();
    std::erase_if(overrides_, [collectionAssetId, stableId](const RenderMaterialParameterCollectionRuntimeValue& value) {
        return value.collectionAssetId == collectionAssetId && value.stableId == stableId;
    });
    if (overrides_.size() == before) {
        return false;
    }
    ++revision_;
    return true;
}

bool RenderMaterialParameterCollectionRuntimeStore::UnloadCollection(std::uint64_t collectionAssetId) {
    const std::size_t defaultCount = defaults_.size();
    const std::size_t overrideCount = overrides_.size();
    std::erase_if(defaults_, [collectionAssetId](const RenderMaterialParameterCollectionRuntimeValue& value) {
        return value.collectionAssetId == collectionAssetId;
    });
    std::erase_if(overrides_, [collectionAssetId](const RenderMaterialParameterCollectionRuntimeValue& value) {
        return value.collectionAssetId == collectionAssetId;
    });
    if (defaults_.size() == defaultCount && overrides_.size() == overrideCount) {
        return false;
    }
    ++revision_;
    return true;
}

bool RenderMaterialParameterCollectionRuntimeStore::RenameParameterStableId(
    std::uint64_t collectionAssetId,
    std::string_view oldStableId,
    std::string_view newStableId) {
    if (collectionAssetId == 0U || oldStableId.empty() || newStableId.empty() || oldStableId == newStableId) {
        return false;
    }
    bool changed = false;
    const auto rename = [&](std::vector<RenderMaterialParameterCollectionRuntimeValue>& values) {
        for (RenderMaterialParameterCollectionRuntimeValue& existing : values) {
            if (existing.collectionAssetId == collectionAssetId && existing.stableId == oldStableId) {
                existing.stableId = std::string{ newStableId };
                changed = true;
            }
        }
    };
    rename(defaults_);
    rename(overrides_);
    if (changed) {
        ++revision_;
    }
    return changed;
}

std::optional<RenderMaterialParameterCollectionRuntimeValue> RenderMaterialParameterCollectionRuntimeStore::Resolve(
    std::uint64_t collectionAssetId,
    std::string_view stableId) const {
    for (const RenderMaterialParameterCollectionRuntimeValue& value : overrides_) {
        if (value.collectionAssetId == collectionAssetId && value.stableId == stableId) {
            return value;
        }
    }
    for (const RenderMaterialParameterCollectionRuntimeValue& value : defaults_) {
        if (value.collectionAssetId == collectionAssetId && value.stableId == stableId) {
            return value;
        }
    }
    return std::nullopt;
}

RenderMaterialParameterCollectionRuntimeStore& GlobalRenderMaterialParameterCollectionStore() noexcept {
    static RenderMaterialParameterCollectionRuntimeStore store;
    return store;
}

std::uint64_t RenderMaterialGraphCollectionAssetId(const RenderMaterialGraphNode& node) noexcept {
    std::uint64_t assetId = 0U;
    const std::string_view hint = Trim(node.parameter.defaultValueHint);
    if (hint.empty() || hint == "_") {
        return 0U;
    }
    return ParseUInt64(hint, assetId) ? assetId : 0U;
}

std::vector<std::uint64_t> DiscoverRenderMaterialGraphParameterCollectionDependencies(const RenderMaterialGraphDocument& graph) {
    std::vector<std::uint64_t> dependencies;
    for (const RenderMaterialGraphNode& node : graph.nodes) {
        if (node.kind != RenderMaterialGraphNodeKind::CollectionParameter) {
            continue;
        }
        const std::uint64_t assetId = RenderMaterialGraphCollectionAssetId(node);
        if (assetId != 0U && std::ranges::find(dependencies, assetId) == dependencies.end()) {
            dependencies.push_back(assetId);
        }
    }
    return dependencies;
}

const RenderMaterialGraphNode* FindRenderMaterialGraphCollectionParameter(
    const RenderMaterialGraphDocument& graph,
    std::uint64_t collectionAssetId,
    std::string_view stableId) noexcept {
    if (collectionAssetId == 0U || stableId.empty()) {
        return nullptr;
    }
    for (const RenderMaterialGraphNode& node : graph.nodes) {
        if (node.kind == RenderMaterialGraphNodeKind::CollectionParameter &&
            RenderMaterialGraphCollectionAssetId(node) == collectionAssetId &&
            node.parameter.stableId == stableId) {
            return &node;
        }
    }
    return nullptr;
}

} // namespace kb::render
