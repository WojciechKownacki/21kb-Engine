#include "kb/net/Rpc.hpp"

#include "kb/net/ByteIO.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <iterator>

namespace kb::net {

namespace {

constexpr std::uint64_t kFnvOffset = 0xCBF29CE484222325ULL;
constexpr std::uint64_t kFnvPrime = 0x100000001B3ULL;

void Mix(std::uint64_t& hash, std::uint64_t value) noexcept {
    for (unsigned shift = 0U; shift < 64U; shift += 8U) {
        hash ^= (value >> shift) & 0xFFU;
        hash *= kFnvPrime;
    }
}

void MixText(std::uint64_t& hash, const std::string& text) noexcept {
    Mix(hash, text.size());
    for (const char character : text) {
        hash ^= static_cast<std::uint8_t>(character);
        hash *= kFnvPrime;
    }
}

[[nodiscard]] bool InRange(const RpcArgSpec& spec, double value) noexcept {
    return std::isfinite(value) && value >= spec.min && value <= spec.max;
}

} // namespace

bool RpcRegistry::Define(RpcDefinition definition, std::string& error) {
    if (definition.name.empty() || definition.name.size() > 64U) {
        error = "an RPC needs a name of 1 to 64 characters";
        return false;
    }
    if (FindByNameUnchecked(definition.name)) {
        error = "the RPC '" + definition.name + "' is already defined";
        return false;
    }
    if (definitions_.size() >= 0xFFFEU) {
        error = "too many RPCs";
        return false;
    }
    if (definition.args.size() > kMaxRpcArgs) {
        error = "the RPC '" + definition.name + "' has too many arguments";
        return false;
    }
    for (const RpcArgSpec& spec : definition.args) {
        if (std::isnan(spec.min) || std::isnan(spec.max) || spec.min > spec.max) {
            error = "the RPC '" + definition.name + "' has an argument with an inverted range";
            return false;
        }
    }
    if (definition.ownerArg != -1) {
        const bool valid = definition.direction == RpcDirection::ClientToServer && definition.ownerArg >= 0 &&
            static_cast<std::size_t>(definition.ownerArg) < definition.args.size() &&
            definition.args[static_cast<std::size_t>(definition.ownerArg)].type == RpcArgType::NetworkId;
        if (!valid) {
            error = "the RPC '" + definition.name + "' names an owner argument that is not a NetworkId of a client call";
            return false;
        }
    }
    if (definition.maxCallsPerSecond == 0U) {
        error = "the RPC '" + definition.name + "' allows no calls";
        return false;
    }
    definitions_.push_back(std::move(definition));
    return true;
}

bool RpcRegistry::FindByNameUnchecked(const std::string& name) const noexcept {
    return std::ranges::any_of(definitions_, [&name](const RpcDefinition& definition) { return definition.name == name; });
}

const RpcDefinition* RpcRegistry::Find(std::uint16_t id) const noexcept {
    return id == 0U || id > definitions_.size() ? nullptr : &definitions_[static_cast<std::size_t>(id) - 1U];
}

const RpcDefinition* RpcRegistry::FindByName(const std::string& name, std::uint16_t& id) const noexcept {
    for (std::size_t index = 0U; index < definitions_.size(); ++index) {
        if (definitions_[index].name == name) {
            id = static_cast<std::uint16_t>(index + 1U);
            return &definitions_[index];
        }
    }
    return nullptr;
}

std::uint64_t RpcRegistry::Fingerprint() const noexcept {
    std::uint64_t hash = kFnvOffset;
    Mix(hash, definitions_.size());
    for (const RpcDefinition& definition : definitions_) {
        MixText(hash, definition.name);
        Mix(hash, static_cast<std::uint64_t>(definition.direction));
        Mix(hash, definition.reliable ? 1U : 0U);
        Mix(hash, static_cast<std::uint64_t>(static_cast<std::int64_t>(definition.ownerArg)));
        Mix(hash, definition.maxCallsPerSecond);
        Mix(hash, definition.args.size());
        for (const RpcArgSpec& spec : definition.args) {
            Mix(hash, static_cast<std::uint64_t>(spec.type));
            Mix(hash, std::bit_cast<std::uint64_t>(spec.min));
            Mix(hash, std::bit_cast<std::uint64_t>(spec.max));
            Mix(hash, spec.maxLength);
        }
    }
    return hash;
}

RpcStatus ValidateRpcArguments(const RpcDefinition& definition, std::span<const RpcValue> args) {
    if (args.size() != definition.args.size()) {
        return RpcStatus::BadArguments;
    }
    for (std::size_t index = 0U; index < args.size(); ++index) {
        const RpcArgSpec& spec = definition.args[index];
        const RpcValue& value = args[index];
        switch (spec.type) {
        case RpcArgType::Bool:
            if (!std::holds_alternative<bool>(value)) {
                return RpcStatus::BadArguments;
            }
            break;
        case RpcArgType::Int32:
            if (!std::holds_alternative<std::int32_t>(value) || !InRange(spec, static_cast<double>(std::get<std::int32_t>(value)))) {
                return RpcStatus::BadArguments;
            }
            break;
        case RpcArgType::UInt32:
            if (!std::holds_alternative<std::uint32_t>(value) || !InRange(spec, static_cast<double>(std::get<std::uint32_t>(value)))) {
                return RpcStatus::BadArguments;
            }
            break;
        case RpcArgType::NetworkId:
            if (!std::holds_alternative<std::uint32_t>(value) || std::get<std::uint32_t>(value) == 0U) {
                return RpcStatus::BadArguments;
            }
            break;
        case RpcArgType::Float:
            if (!std::holds_alternative<float>(value) || !InRange(spec, static_cast<double>(std::get<float>(value)))) {
                return RpcStatus::BadArguments;
            }
            break;
        case RpcArgType::String:
            if (!std::holds_alternative<std::string>(value) || std::get<std::string>(value).size() > spec.maxLength) {
                return RpcStatus::BadArguments;
            }
            break;
        }
    }
    return RpcStatus::Ok;
}

RpcStatus EncodeRpc(const RpcRegistry& registry, std::uint16_t id, std::span<const RpcValue> args, std::vector<std::uint8_t>& out) {
    const RpcDefinition* definition = registry.Find(id);
    if (definition == nullptr) {
        return RpcStatus::UnknownRpc;
    }
    if (const RpcStatus status = ValidateRpcArguments(*definition, args); status != RpcStatus::Ok) {
        return status;
    }
    const std::size_t start = out.size();
    ByteWriter writer(out);
    writer.U16(id);
    for (const RpcValue& value : args) {
        if (const auto* flag = std::get_if<bool>(&value)) {
            writer.U8(*flag ? 1U : 0U);
        } else if (const auto* signedValue = std::get_if<std::int32_t>(&value)) {
            writer.U32(static_cast<std::uint32_t>(*signedValue));
        } else if (const auto* unsignedValue = std::get_if<std::uint32_t>(&value)) {
            writer.U32(*unsignedValue);
        } else if (const auto* real = std::get_if<float>(&value)) {
            writer.U32(std::bit_cast<std::uint32_t>(*real));
        } else {
            const std::string& text = std::get<std::string>(value);
            writer.U16(static_cast<std::uint16_t>(text.size()));
            writer.Bytes(std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t*>(text.data()), text.size()));
        }
    }
    if (out.size() - start > kMaxRpcPayloadBytes) {
        out.resize(start);
        return RpcStatus::TooLarge;
    }
    return RpcStatus::Ok;
}

RpcStatus DecodeRpc(const RpcRegistry& registry, std::span<const std::uint8_t> payload, RpcDirection expected, std::uint16_t& id,
    std::vector<RpcValue>& args) {
    args.clear();
    if (payload.size() > kMaxRpcPayloadBytes) {
        return RpcStatus::TooLarge;
    }
    ByteReader reader(payload);
    const std::uint16_t callId = reader.U16();
    if (!reader.Ok()) {
        return RpcStatus::Malformed;
    }
    const RpcDefinition* definition = registry.Find(callId);
    if (definition == nullptr) {
        return RpcStatus::UnknownRpc;
    }
    if (definition->direction != expected) {
        return RpcStatus::WrongDirection;
    }
    for (const RpcArgSpec& spec : definition->args) {
        switch (spec.type) {
        case RpcArgType::Bool: {
            const std::uint8_t byte = reader.U8();
            if (byte > 1U) {
                return RpcStatus::Malformed;
            }
            args.emplace_back(byte == 1U);
            break;
        }
        case RpcArgType::Int32:
            args.emplace_back(static_cast<std::int32_t>(reader.U32()));
            break;
        case RpcArgType::UInt32:
        case RpcArgType::NetworkId:
            args.emplace_back(reader.U32());
            break;
        case RpcArgType::Float:
            args.emplace_back(std::bit_cast<float>(reader.U32()));
            break;
        case RpcArgType::String: {
            const std::uint16_t length = reader.U16();
            if (length > spec.maxLength) {
                return reader.Ok() ? RpcStatus::BadArguments : RpcStatus::Malformed;
            }
            const std::span<const std::uint8_t> bytes = reader.Bytes(length);
            args.emplace_back(std::string(reinterpret_cast<const char*>(bytes.data()), bytes.size()));
            break;
        }
        }
        if (!reader.Ok()) {
            return RpcStatus::Malformed;
        }
    }
    if (reader.Remaining() != 0U) {
        return RpcStatus::Malformed;
    }
    if (const RpcStatus status = ValidateRpcArguments(*definition, args); status != RpcStatus::Ok) {
        return status;
    }
    id = callId;
    return RpcStatus::Ok;
}

bool RpcRateLimiter::Allow(std::uint64_t caller, std::uint16_t rpc, std::uint32_t perSecond, std::uint64_t nowMilliseconds) {
    Bucket& bucket = buckets_[{ caller, rpc }];
    const double capacity = static_cast<double>(perSecond);
    if (!bucket.initialised) {
        bucket.initialised = true;
        bucket.tokens = capacity;
        bucket.lastMilliseconds = nowMilliseconds;
    } else if (nowMilliseconds > bucket.lastMilliseconds) {
        bucket.tokens = std::min(capacity, bucket.tokens + static_cast<double>(nowMilliseconds - bucket.lastMilliseconds) * capacity / 1000.0);
        bucket.lastMilliseconds = nowMilliseconds;
    }
    if (bucket.tokens < 1.0) {
        return false;
    }
    bucket.tokens -= 1.0;
    return true;
}

void RpcRateLimiter::Forget(std::uint64_t caller) {
    for (auto it = buckets_.begin(); it != buckets_.end();) {
        it = it->first.first == caller ? buckets_.erase(it) : std::next(it);
    }
}

} // namespace kb::net
