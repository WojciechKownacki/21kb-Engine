#include "scene/ui/SceneUIComponentTextCodec.hpp"

#include "engine/scene/SceneDocument.hpp"

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

// Prefab assets and overrides store UI components as text. The payload layout follows the scene
// format version, so the text says which version wrote it: "v36:" then hex. Text without a prefix
// was written before versions were recorded, when the layout was that of v34.
constexpr std::string_view kVersionPrefix = "v";
constexpr std::uint32_t kUnversionedTextLayout = 34U;

std::uint32_t SceneUIComponentTextCodec::EncodedVersion(std::string_view encoded) noexcept {
    if (!encoded.starts_with(kVersionPrefix)) return kUnversionedTextLayout;
    const std::size_t colon = encoded.find(':');
    if (colon == std::string_view::npos || colon == kVersionPrefix.size()) return 0U;
    std::uint32_t parsed = 0U;
    for (const char digit : encoded.substr(kVersionPrefix.size(), colon - kVersionPrefix.size())) {
        if (digit < '0' || digit > '9' || parsed > SceneDocument::CurrentFileVersion) return 0U;
        parsed = parsed * 10U + static_cast<std::uint32_t>(digit - '0');
    }
    return parsed < kUnversionedTextLayout || parsed > SceneDocument::CurrentFileVersion ? 0U : parsed;
}

std::string SceneUIComponentTextCodec::Encode(const UIComponentSet& components) {
    if (components.Empty()) return {};
    std::vector<std::uint8_t> bytes;
    SceneAssetUIComponentCodec::Write(bytes, components);
    constexpr char digits[] = "0123456789ABCDEF";
    std::string encoded = std::string{kVersionPrefix} + std::to_string(SceneDocument::CurrentFileVersion) + ':';
    const std::size_t header = encoded.size();
    encoded.resize(header + bytes.size() * 2U);
    for (std::size_t index = 0U; index < bytes.size(); ++index) {
        encoded[header + index * 2U] = digits[bytes[index] >> 4U];
        encoded[header + index * 2U + 1U] = digits[bytes[index] & 0xFU];
    }
    return encoded;
}

bool SceneUIComponentTextCodec::Decode(std::string_view encoded, UIComponentSet& output) {
    if (encoded.empty()) {
        output = {};
        return true;
    }
    const std::uint32_t version = EncodedVersion(encoded);
    if (version == 0U) return false;
    if (encoded.starts_with(kVersionPrefix)) encoded.remove_prefix(encoded.find(':') + 1U);
    if ((encoded.size() & 1U) != 0U || encoded.size() > 65536U) return false;
    std::vector<std::uint8_t> bytes(encoded.size() / 2U);
    for (std::size_t index = 0U; index < bytes.size(); ++index) {
        const int high = HexDigit(encoded[index * 2U]);
        const int low = HexDigit(encoded[index * 2U + 1U]);
        if (high < 0 || low < 0) return false;
        bytes[index] = static_cast<std::uint8_t>((high << 4) | low);
    }
    UIComponentSet decoded;
    SceneAssetBinaryIO::ByteReader reader{std::move(bytes)};
    if (!SceneAssetUIComponentCodec::Read(reader, version, decoded) || decoded.Empty() || !reader.Exhausted()) return false;
    output = std::move(decoded);
    return true;
}

} // namespace kb::scene
