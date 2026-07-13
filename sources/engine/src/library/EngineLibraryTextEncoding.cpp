#include "engine/library/EngineLibraryTextEncoding.hpp"

namespace kb::library {

bool IsValidUtf8(std::string_view text) noexcept {
    std::size_t index = 0U;
    const std::size_t length = text.size();
    while (index < length) {
        const auto leadByte = static_cast<unsigned char>(text[index]);

        if (leadByte <= 0x7FU) {
            ++index;
            continue;
        }

        std::size_t continuationCount = 0U;
        unsigned char firstContinuationLow = 0x80U;
        unsigned char firstContinuationHigh = 0xBFU;

        if (leadByte >= 0xC2U && leadByte <= 0xDFU) {
            continuationCount = 1U;
        } else if (leadByte == 0xE0U) {
            continuationCount = 2U;
            firstContinuationLow = 0xA0U;
        } else if (leadByte >= 0xE1U && leadByte <= 0xECU) {
            continuationCount = 2U;
        } else if (leadByte == 0xEDU) {
            // Rejects UTF-16 surrogate halves (U+D800..U+DFFF), which
            // well-formed UTF-8 must never encode.
            continuationCount = 2U;
            firstContinuationHigh = 0x9FU;
        } else if (leadByte >= 0xEEU && leadByte <= 0xEFU) {
            continuationCount = 2U;
        } else if (leadByte == 0xF0U) {
            continuationCount = 3U;
            firstContinuationLow = 0x90U;
        } else if (leadByte >= 0xF1U && leadByte <= 0xF3U) {
            continuationCount = 3U;
        } else if (leadByte == 0xF4U) {
            // Caps codepoints at U+10FFFF, the top of the Unicode range.
            continuationCount = 3U;
            firstContinuationHigh = 0x8FU;
        } else {
            // 0x80-0xC1: stray continuation byte or an overlong 2-byte
            // lead (0xC0/0xC1 can only encode codepoints below U+0080,
            // which must be 1-byte ASCII instead). 0xF5-0xFF: beyond
            // U+10FFFF. Both are always invalid as a lead byte.
            return false;
        }

        if (index + continuationCount >= length) {
            return false;
        }

        for (std::size_t offset = 1U; offset <= continuationCount; ++offset) {
            const auto continuationByte = static_cast<unsigned char>(text[index + offset]);
            const unsigned char low = offset == 1U ? firstContinuationLow : 0x80U;
            const unsigned char high = offset == 1U ? firstContinuationHigh : 0xBFU;
            if (continuationByte < low || continuationByte > high) {
                return false;
            }
        }

        index += continuationCount + 1U;
    }
    return true;
}

} // namespace kb::library
