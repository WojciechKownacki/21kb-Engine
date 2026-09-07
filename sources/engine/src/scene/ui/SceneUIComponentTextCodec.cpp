#include "scene/ui/SceneUIComponentTextCodec.hpp"

#include "scene/asset/io/components/SceneAssetUIComponentCodec.hpp"

#include <utility>
#include <vector>

namespace kb::scene {
namespace {

[[nodiscard]] int HexDigit(char value) noexcept {
    if (value >= '0' && value <= '9') return value - '0';
    if (value >= 'a' && value <= 'f') return value - 'a' + 10;
    if (value >= 'A' && value <= 'F') return value - 'A' + 10;
    return -1;
}

} // namespace

std::string SceneUIComponentTextCodec::Encode(const UIComponentSet& components) {
    if (components.Empty()) return {};
    std::vector<std::uint8_t> bytes;
    SceneAssetUIComponentCodec::Write(bytes, components);
    constexpr char digits[] = "0123456789ABCDEF";
    std::string encoded(bytes.size() * 2U, '\0');
    for (std::size_t index = 0U; index < bytes.size(); ++index) {
        encoded[index * 2U] = digits[bytes[index] >> 4U];
        encoded[index * 2U + 1U] = digits[bytes[index] & 0xFU];
    }
    return encoded;
}

bool SceneUIComponentTextCodec::Decode(std::string_view encoded, UIComponentSet& output) {
    if (encoded.empty()) {
        output = {};
        return true;
    }
    if ((encoded.size() & 1U) != 0U || encoded.size() > 16384U) return false;
    std::vector<std::uint8_t> bytes(encoded.size() / 2U);
    for (std::size_t index = 0U; index < bytes.size(); ++index) {
        const int high = HexDigit(encoded[index * 2U]);
        const int low = HexDigit(encoded[index * 2U + 1U]);
        if (high < 0 || low < 0) return false;
        bytes[index] = static_cast<std::uint8_t>((high << 4) | low);
    }
    UIComponentSet decoded;
    SceneAssetBinaryIO::ByteReader reader{std::move(bytes)};
    if (!SceneAssetUIComponentCodec::Read(reader, decoded) || decoded.Empty() || !reader.Exhausted()) return false;
    output = std::move(decoded);
    return true;
}

} // namespace kb::scene
