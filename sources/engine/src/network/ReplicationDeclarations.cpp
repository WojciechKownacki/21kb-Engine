#include "engine/network/ReplicationDeclarations.hpp"
#include "engine/core/JsonValue.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <set>
#include <tuple>
#include <limits>

namespace kb::network {
namespace {
using Json = kb::core::JsonValue;

bool Number(const Json& object, const char* key, double fallback, double& out, const std::string& label, std::string& error) {
    const auto* value = object.Find(key);
    out = value == nullptr ? fallback : value->AsNumber();
    if ((value != nullptr && value->GetKind() != Json::Kind::Number) || !std::isfinite(out)) {
        error = label + ": " + key + " must be a finite number";
        return false;
    }
    return true;
}
bool Audience(const Json& object, ReplicationAudience& out, const std::string& label, std::string& error) {
    const auto* value = object.Find("audience");
    if (value == nullptr) return true;
    for (const auto audience : {ReplicationAudience::Everyone, ReplicationAudience::Owner, ReplicationAudience::Others, ReplicationAudience::InitialOnly}) {
        if (value->GetKind() == Json::Kind::String && value->AsString() == ReplicationAudienceName(audience)) { out = audience; return true; }
    }
    error = label + ": unknown audience";
    return false;
}
bool Name(std::string_view name) {
    if (name.empty() || name.size() > 32U || (!std::isalpha(static_cast<unsigned char>(name.front())) && name.front() != '_')) return false;
    return std::ranges::all_of(name, [](unsigned char value) { return std::isalnum(value) != 0 || value == '_'; });
}
bool Range(const Json& object, float& low, float& high, std::uint8_t& bits, const std::string& label, std::string& error, bool required) {
    double minimum, maximum, count;
    if (!Number(object, "minimum", low, minimum, label, error) || !Number(object, "maximum", high, maximum, label, error) ||
        !Number(object, "bits", bits, count, label, error)) return false;
    low = static_cast<float>(minimum);
    high = static_cast<float>(maximum);
    if (!std::isfinite(low) || !std::isfinite(high) || (required && (low >= high || static_cast<double>(high) - low > std::numeric_limits<float>::max()))) { error = label + ": minimum must be less than maximum with a finite float span"; return false; }
    if (count != std::floor(count) || count < (required ? 1.0 : 0.0) || count > 24.0) { error = label + ": bits must be in 1..24"; return false; }
    bits = static_cast<std::uint8_t>(count);
    return true;
}
} // namespace

bool ParseReplicationDeclarations(std::string_view json, ReplicationDeclarations& out, std::string& error) {
    Json root;
    if (!Json::Parse(json, root, error)) return false;
    if (root.GetKind() != Json::Kind::Object) { error = "replication declarations must be an object"; return false; }
    const auto* version = root.Find("version");
    if (version == nullptr || version->GetKind() != Json::Kind::Number || version->AsNumber() != 1.0) { error = "unsupported replication version"; return false; }
    ReplicationDeclarations parsed;
    std::set<std::string> fields, names, groups;
    if (const auto* list = root.Find("fields")) {
        if (list->GetKind() != Json::Kind::Array) { error = "fields must be an array"; return false; }
        for (std::size_t i = 0; i < list->Size(); ++i) {
            const auto& item = *list->At(i);
            ReplicatedFieldDecl field;
            const auto* component = item.Find("component");
            const auto* member = item.Find("field");
            if (component == nullptr || member == nullptr || component->GetKind() != Json::Kind::String || member->GetKind() != Json::Kind::String || component->AsString().empty() || member->AsString().empty()) {
                error = "field declaration requires component and field names"; return false;
            }
            field.component = component->AsString();
            field.field = member->AsString();
            const auto label = field.component + "." + field.field;
            if (!fields.insert(label).second) { error = label + ": duplicate field"; return false; }
            if (!Audience(item, field.audience, label, error)) return false;
            const bool quantized = item.Find("bits") != nullptr && item.Find("bits")->AsNumber() != 0.0;
            if (!Range(item, field.minimum, field.maximum, field.bits, label, error, quantized)) return false;
            double maximum;
            if (!Number(item, "maxUInt", 0.0, maximum, label, error)) return false;
            if (maximum < 0.0 || maximum > UINT32_MAX || maximum != std::floor(maximum)) { error = label + ": invalid maxUInt"; return false; }
            field.maxUInt = static_cast<std::uint32_t>(maximum);
            groups.insert(field.component + "/" + std::string(ReplicationAudienceName(field.audience)));
            parsed.fields.push_back(std::move(field));
        }
    }
    if (const auto* list = root.Find("variables")) {
        if (list->GetKind() != Json::Kind::Array || list->Size() > 32U) { error = "variables must be an array of at most 32 declarations"; return false; }
        for (std::size_t i = 0; i < list->Size(); ++i) {
            const auto& item = *list->At(i);
            ReplicatedVariableDecl variable;
            const auto* name = item.Find("name");
            const auto* type = item.Find("type");
            if (name == nullptr || name->GetKind() != Json::Kind::String || !Name(name->AsString())) { error = "invalid variable name"; return false; }
            variable.name = name->AsString();
            if (!names.insert(variable.name).second) { error = variable.name + ": duplicate variable"; return false; }
            const std::string typeName = type == nullptr ? "Float" : type->AsString();
            if (typeName == "Float") variable.type = ReplicatedVariableDecl::Type::Float;
            else if (typeName == "Int") variable.type = ReplicatedVariableDecl::Type::Int;
            else if (typeName == "Bool") { variable.type = ReplicatedVariableDecl::Type::Bool; variable.maximum = 1.0F; variable.bits = 1U; }
            else { error = variable.name + ": unknown variable type"; return false; }
            if (!Audience(item, variable.audience, variable.name, error) ||
                !Range(item, variable.minimum, variable.maximum, variable.bits, variable.name, error, variable.type == ReplicatedVariableDecl::Type::Float)) return false;
            if (variable.type == ReplicatedVariableDecl::Type::Int && (variable.minimum >= variable.maximum || variable.minimum < -16'777'216.0F || variable.maximum > 16'777'216.0F ||
                variable.minimum != std::floor(variable.minimum) || variable.maximum != std::floor(variable.maximum))) { error = variable.name + ": Int range must contain whole numbers within +/-16777216"; return false; }
            if (variable.type == ReplicatedVariableDecl::Type::Bool && (variable.minimum != 0.0F || variable.maximum != 1.0F)) { error = variable.name + ": Bool range must be 0..1"; return false; }
            groups.insert("variables/" + std::string(ReplicationAudienceName(variable.audience)));
            parsed.variables.push_back(std::move(variable));
        }
    }
    if (groups.size() > 29U) { error = "replication declarations exceed 29 groups"; return false; }
    out = std::move(parsed);
    error.clear();
    return true;
}

std::string WriteReplicationDeclarations(const ReplicationDeclarations& declarations) {
    auto root = Json::MakeObject();
    root.Set("version", Json::MakeNumber(declarations.version));
    auto fields = Json::MakeArray();
    auto sorted = declarations.fields;
    std::ranges::sort(sorted, [](const auto& a, const auto& b) { return std::tie(a.component, a.audience, a.field) < std::tie(b.component, b.audience, b.field); });
    for (const auto& field : sorted) {
        auto item = Json::MakeObject();
        item.Set("component", Json::MakeString(field.component)); item.Set("field", Json::MakeString(field.field));
        item.Set("audience", Json::MakeString(std::string(ReplicationAudienceName(field.audience))));
        item.Set("minimum", Json::MakeNumber(field.minimum)); item.Set("maximum", Json::MakeNumber(field.maximum));
        item.Set("bits", Json::MakeNumber(field.bits)); item.Set("maxUInt", Json::MakeNumber(field.maxUInt));
        fields.Append(std::move(item));
    }
    root.Set("fields", std::move(fields));
    auto variables = Json::MakeArray();
    for (const auto& variable : declarations.variables) {
        auto item = Json::MakeObject();
        item.Set("name", Json::MakeString(variable.name));
        item.Set("type", Json::MakeString(variable.type == ReplicatedVariableDecl::Type::Float ? "Float" : variable.type == ReplicatedVariableDecl::Type::Int ? "Int" : "Bool"));
        item.Set("audience", Json::MakeString(std::string(ReplicationAudienceName(variable.audience))));
        item.Set("minimum", Json::MakeNumber(variable.minimum)); item.Set("maximum", Json::MakeNumber(variable.maximum)); item.Set("bits", Json::MakeNumber(variable.bits));
        variables.Append(std::move(item));
    }
    root.Set("variables", std::move(variables));
    return root.Dump();
}

} // namespace kb::network
