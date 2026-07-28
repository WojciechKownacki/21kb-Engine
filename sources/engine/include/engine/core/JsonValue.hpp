#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace kb::core {

// Small strict JSON document model shared by engine-owned tools and editor
// automation. It deliberately has no filesystem or schema policy: consumers
// validate their own documents and keep domain contracts out of the parser.
class JsonValue final {
public:
    enum class Kind {
        Null,
        Bool,
        Number,
        String,
        Array,
        Object,
    };

    JsonValue() = default;

    [[nodiscard]] static JsonValue MakeNull();
    [[nodiscard]] static JsonValue MakeBool(bool value);
    [[nodiscard]] static JsonValue MakeNumber(double value);
    [[nodiscard]] static JsonValue MakeString(std::string value);
    [[nodiscard]] static JsonValue MakeArray();
    [[nodiscard]] static JsonValue MakeObject();

    [[nodiscard]] Kind GetKind() const noexcept;
    [[nodiscard]] bool IsNull() const noexcept;
    [[nodiscard]] bool AsBool(bool fallback = false) const noexcept;
    [[nodiscard]] double AsNumber(double fallback = 0.0) const noexcept;
    [[nodiscard]] const std::string& AsString() const noexcept;

    [[nodiscard]] std::size_t Size() const noexcept;
    [[nodiscard]] const JsonValue* At(std::size_t index) const noexcept;
    void Append(JsonValue value);

    [[nodiscard]] const JsonValue* Find(std::string_view key) const noexcept;
    [[nodiscard]] const std::string& MemberName(std::size_t index) const noexcept;
    void Set(std::string key, JsonValue value);

    [[nodiscard]] std::string Dump() const;

    [[nodiscard]] static bool Parse(std::string_view text, JsonValue& out, std::string& error);

private:
    Kind kind_ = Kind::Null;
    bool bool_ = false;
    double number_ = 0.0;
    std::string string_;
    std::vector<JsonValue> array_;
    std::vector<std::string> memberNames_;
    std::vector<JsonValue> memberValues_;
};

} // namespace kb::core
