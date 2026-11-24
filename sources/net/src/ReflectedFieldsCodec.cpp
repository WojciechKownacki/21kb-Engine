#include "ReflectedFieldsCodec.hpp"
#include "FieldEncoding.hpp"
#include "engine/network/ReplicableComponents.hpp"
#include "engine/ecs/ComponentFieldAccess.hpp"
#include "engine/scene/NetworkedVariablesComponent.hpp"

#include <cstring>

namespace kb::net {
namespace {
using kb::ecs::ComponentFieldType;

class ReflectedFieldsCodec final : public ComponentCodec {
public:
    struct Field { kb::ecs::ComponentFieldReflection reflection; FieldEncoding encoding; };
    ReflectedFieldsCodec(const kb::ecs::World& world, kb::network::ReplicableComponent component, std::vector<Field> fields, ReplicationAudience audience, bool removeWhole)
        : ComponentCodec(audience), component_(component), fields_(std::move(fields)), removeWhole_(removeWhole),
          scratch_(component.reflection->Size()), defaults_(component.reflection->Size()) {
        static_cast<void>(component_.read(world, {}, defaults_));
        name_ = std::string(component.name) + "." + std::string(kb::network::ReplicationAudienceName(audience));
        for (const auto& field : fields_) size_ += field.encoding.Size();
    }
    const std::string& Name() const noexcept override { return name_; }
    bool Valid() const noexcept override { return size_ != 0U; }
    std::size_t EncodedSize() const noexcept override { return size_; }
    kb::ecs::ComponentId SourceComponent(kb::ecs::World& world) const override { return component_.source(world); }
    std::array<kb::ecs::ObserverId, 3U> ObserveChanges(kb::ecs::World& world, ReplicationChangeSink& sink) const override { return component_.observe(world, sink); }
    std::string Describe() const override {
        std::string result = name_;
        for (const auto& field : fields_) result += "|" + field.reflection.name + ":" + std::to_string(static_cast<int>(field.reflection.type)) + ":" + field.encoding.Describe();
        return result;
    }
    bool Capture(const kb::ecs::World& world, kb::ecs::Entity entity, std::span<std::uint8_t> out) const override {
        if (out.size() != size_ || !component_.read(world, entity, scratch_)) return false;
        std::size_t offset = 0U;
        for (const auto& field : fields_) {
            std::array<std::byte, 4> raw{};
            if (!kb::ecs::ComponentFieldAccess::Read(scratch_.data(), scratch_.size(), field.reflection, std::span(raw).first(field.reflection.size))) return false;
            double value;
            if (field.reflection.type == ComponentFieldType::Float32) { float v; std::memcpy(&v, raw.data(), sizeof(v)); value = v; }
            else if (field.reflection.type == ComponentFieldType::UInt32) { std::uint32_t v; std::memcpy(&v, raw.data(), sizeof(v)); value = v; }
            else { bool v; std::memcpy(&v, raw.data(), sizeof(v)); value = v ? 1.0 : 0.0; }
            field.encoding.Encode(value, out.subspan(offset, field.encoding.Size()));
            offset += field.encoding.Size();
        }
        return true;
    }
    bool Validate(std::span<const std::uint8_t> bytes) const override {
        if (bytes.size() != size_) return false;
        std::size_t offset = 0U;
        for (const auto& field : fields_) { double value; if (!field.encoding.Decode(bytes.subspan(offset, field.encoding.Size()), value)) return false; offset += field.encoding.Size(); }
        return true;
    }
    void Apply(kb::ecs::World& world, kb::ecs::Entity entity, std::span<const std::uint8_t> bytes) const override {
        if (!Validate(bytes)) return;
        static_cast<void>(component_.read(world, entity, scratch_));
        std::size_t offset = 0U;
        for (const auto& field : fields_) {
            double decoded;
            static_cast<void>(field.encoding.Decode(bytes.subspan(offset, field.encoding.Size()), decoded));
            std::array<std::byte, 4> raw{};
            if (field.reflection.type == ComponentFieldType::Float32) { const float value = static_cast<float>(decoded); std::memcpy(raw.data(), &value, sizeof(value)); }
            else if (field.reflection.type == ComponentFieldType::UInt32) { const auto value = static_cast<std::uint32_t>(decoded); std::memcpy(raw.data(), &value, sizeof(value)); }
            else { const bool value = decoded != 0.0; std::memcpy(raw.data(), &value, sizeof(value)); }
            static_cast<void>(kb::ecs::ComponentFieldAccess::Write(scratch_.data(), scratch_.size(), field.reflection, std::span<const std::byte>(raw).first(field.reflection.size)));
            offset += field.encoding.Size();
        }
        static_cast<void>(component_.write(world, entity, scratch_));
    }
    void Remove(kb::ecs::World& world, kb::ecs::Entity entity) const override {
        if (removeWhole_) { component_.remove(world, entity); return; }
        if (!component_.read(world, entity, scratch_)) return;
        for (const auto& field : fields_) {
            const auto raw = std::span<const std::byte>(defaults_).subspan(field.reflection.offset, field.reflection.size);
            static_cast<void>(kb::ecs::ComponentFieldAccess::Write(scratch_.data(), scratch_.size(), field.reflection, raw));
        }
        static_cast<void>(component_.write(world, entity, scratch_));
    }
private:
    kb::network::ReplicableComponent component_;
    std::vector<Field> fields_;
    bool removeWhole_;
    mutable std::vector<std::byte> scratch_;
    std::vector<std::byte> defaults_;
    std::string name_;
    std::size_t size_ = 0U;
};

class NetworkedVariablesCodec final : public ComponentCodec {
public:
    struct Variable { std::size_t index; std::string name; FieldEncoding encoding; };
    NetworkedVariablesCodec(std::vector<Variable> variables, ReplicationAudience audience) : ComponentCodec(audience), variables_(std::move(variables)) {
        name_ = "NetworkedVariables." + std::string(kb::network::ReplicationAudienceName(audience));
        for (const auto& variable : variables_) size_ += variable.encoding.Size();
    }
    const std::string& Name() const noexcept override { return name_; }
    bool Valid() const noexcept override { return size_ != 0U; }
    std::size_t EncodedSize() const noexcept override { return size_; }
    kb::ecs::ComponentId SourceComponent(kb::ecs::World& world) const override { return world.RegisterComponent<kb::scene::NetworkedVariablesComponent>(); }
    std::array<kb::ecs::ObserverId, 3U> ObserveChanges(kb::ecs::World& world, ReplicationChangeSink& sink) const override { return ObserveReplicationChanges<kb::scene::NetworkedVariablesComponent>(world, sink); }
    std::string Describe() const override {
        auto result = name_;
        for (const auto& variable : variables_) result += "|" + variable.name + ":" + std::to_string(variable.index) + ":" + variable.encoding.Describe();
        return result;
    }
    bool Capture(const kb::ecs::World& world, kb::ecs::Entity entity, std::span<std::uint8_t> out) const override {
        const auto* component = world.TryGet<kb::scene::NetworkedVariablesComponent>(entity);
        if (component == nullptr || out.size() != size_) return false;
        std::size_t offset = 0U;
        for (const auto& variable : variables_) { variable.encoding.Encode(component->values[variable.index], out.subspan(offset, variable.encoding.Size())); offset += variable.encoding.Size(); }
        return true;
    }
    bool Validate(std::span<const std::uint8_t> bytes) const override {
        if (bytes.size() != size_) return false;
        std::size_t offset = 0U;
        for (const auto& variable : variables_) { double value; if (!variable.encoding.Decode(bytes.subspan(offset, variable.encoding.Size()), value)) return false; offset += variable.encoding.Size(); }
        return true;
    }
    void Apply(kb::ecs::World& world, kb::ecs::Entity entity, std::span<const std::uint8_t> bytes) const override {
        if (!Validate(bytes)) return;
        const auto* existing = world.TryGet<kb::scene::NetworkedVariablesComponent>(entity);
        auto component = existing == nullptr ? kb::scene::NetworkedVariablesComponent{} : *existing;
        std::size_t offset = 0U;
        for (const auto& variable : variables_) { double value; static_cast<void>(variable.encoding.Decode(bytes.subspan(offset, variable.encoding.Size()), value)); component.values[variable.index] = static_cast<float>(value); offset += variable.encoding.Size(); }
        world.Set(entity, component);
    }
    void Remove(kb::ecs::World& world, kb::ecs::Entity entity) const override {
        const auto* existing = world.TryGet<kb::scene::NetworkedVariablesComponent>(entity);
        if (existing == nullptr) return;
        auto component = *existing;
        for (const auto& variable : variables_) component.values[variable.index] = 0.0F;
        world.Set(entity, component);
    }
private:
    std::vector<Variable> variables_;
    std::string name_;
    std::size_t size_ = 0U;
};
} // namespace

std::unique_ptr<ComponentCodec> MakeReflectedFieldsCodec(const kb::ecs::World& world, std::vector<kb::network::ReplicatedFieldDecl> declarations,
    bool removeWholeComponent, std::string& error) {
    if (declarations.empty()) { error = "empty reflected group"; return nullptr; }
    auto table = kb::network::MakeSceneReplicableComponents(world);
    const auto* component = table.Find(declarations.front().component);
    if (component == nullptr) { error = declarations.front().component + "." + declarations.front().field + ": unknown replicable component"; return nullptr; }
    std::vector<ReflectedFieldsCodec::Field> fields;
    for (const auto& declaration : declarations) {
        const auto label = declaration.component + "." + declaration.field;
        const auto* reflection = component->reflection->FindField(declaration.field);
        if (reflection == nullptr) { error = label + ": unknown field"; return nullptr; }
        FieldEncoding encoding;
        encoding.minimum = declaration.minimum; encoding.maximum = declaration.maximum; encoding.bits = declaration.bits; encoding.maxUInt = declaration.maxUInt;
        if (reflection->type == ComponentFieldType::Float32) encoding.kind = FieldEncoding::Kind::Float;
        else if (reflection->type == ComponentFieldType::UInt32) encoding.kind = FieldEncoding::Kind::UInt;
        else if (reflection->type == ComponentFieldType::Bool) encoding.kind = FieldEncoding::Kind::Bool;
        else { error = label + ": unsupported replicated field type"; return nullptr; }
        if (!encoding.Valid()) { error = label + ": invalid range or bits"; return nullptr; }
        fields.push_back({*reflection, encoding});
    }
    return std::make_unique<ReflectedFieldsCodec>(world, *component, std::move(fields), declarations.front().audience, removeWholeComponent);
}

std::unique_ptr<ComponentCodec> MakeNetworkedVariablesCodec(const kb::network::ReplicationDeclarations& declarations,
    ReplicationAudience audience, std::string& error) {
    std::vector<NetworkedVariablesCodec::Variable> variables;
    for (std::size_t i = 0; i < declarations.variables.size(); ++i) {
        const auto& declaration = declarations.variables[i];
        if (declaration.audience != audience) continue;
        FieldEncoding encoding;
        encoding.minimum = declaration.minimum; encoding.maximum = declaration.maximum; encoding.bits = declaration.bits;
        encoding.kind = declaration.type == kb::network::ReplicatedVariableDecl::Type::Float ? FieldEncoding::Kind::Float :
            declaration.type == kb::network::ReplicatedVariableDecl::Type::Int ? FieldEncoding::Kind::Int : FieldEncoding::Kind::Bool;
        if (!encoding.Valid()) { error = declaration.name + ": invalid variable encoding"; return nullptr; }
        variables.push_back({i, declaration.name, encoding});
    }
    return std::make_unique<NetworkedVariablesCodec>(std::move(variables), audience);
}

} // namespace kb::net
