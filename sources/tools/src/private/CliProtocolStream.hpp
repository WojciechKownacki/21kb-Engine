#pragma once

#include "engine/core/JsonValue.hpp"

#include <initializer_list>
#include <istream>
#include <ostream>
#include <string>
#include <string_view>
#include <utility>

namespace kb::cli {

enum class FramedRead {
    Message,    // `body` holds one message
    Malformed,  // the frame had no usable Content-Length or was too long; the stream is at the next frame
    EndOfInput, // no further complete frame
};

// On Windows a text-mode stdin and stdout change line ends, which breaks the byte count of a frame: when `in` is the
// process's own stdin both are switched to bytes. Other streams (tests) are left alone.
void UseBinaryStdio(const std::istream& in);

// One message of the base protocol that the Language Server and Debug Adapter protocols share:
// "Content-Length: <n>", an empty line, then n bytes.
[[nodiscard]] FramedRead ReadFramedMessage(std::istream& in, std::string& body);

// Writes `message` as one frame and flushes, so the editor sees it at once.
void WriteFramedMessage(std::ostream& out, const kb::core::JsonValue& message);

// The small JSON builders and readers the protocol servers share.
[[nodiscard]] inline kb::core::JsonValue Str(std::string value) { return kb::core::JsonValue::MakeString(std::move(value)); }
[[nodiscard]] inline kb::core::JsonValue Num(double value) { return kb::core::JsonValue::MakeNumber(value); }
[[nodiscard]] inline kb::core::JsonValue Bool(bool value) { return kb::core::JsonValue::MakeBool(value); }

[[nodiscard]] inline kb::core::JsonValue Object(std::initializer_list<std::pair<const char*, kb::core::JsonValue>> members) {
    kb::core::JsonValue object = kb::core::JsonValue::MakeObject();
    for (const auto& [name, value] : members) object.Set(name, value);
    return object;
}

// The member of an object, or null when `value` is not an object or has no such member.
[[nodiscard]] inline const kb::core::JsonValue* Member(const kb::core::JsonValue* value, std::string_view name) {
    return value != nullptr && value->GetKind() == kb::core::JsonValue::Kind::Object ? value->Find(name) : nullptr;
}

[[nodiscard]] inline bool IsString(const kb::core::JsonValue* value) {
    return value != nullptr && value->GetKind() == kb::core::JsonValue::Kind::String;
}

[[nodiscard]] inline bool IsNumber(const kb::core::JsonValue* value) {
    return value != nullptr && value->GetKind() == kb::core::JsonValue::Kind::Number;
}

} // namespace kb::cli
