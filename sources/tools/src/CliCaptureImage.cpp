#include "CliCaptureImage.hpp"

#if KB_CLI_RENDERER
#include <bimg/bimg.h>
#include <bimg/decode.h>
#include <bimg/encode.h>
#include <bx/allocator.h>
#include <bx/error.h>
#include <bx/readerwriter.h>
#endif

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <map>
#include <memory>
#include <string_view>
#include <system_error>

namespace kb::cli {
namespace {

namespace fs = std::filesystem;

[[nodiscard]] std::string Hex(unsigned r, unsigned g, unsigned b) {
    char text[16]{};
    std::snprintf(text, sizeof(text), "#%02x%02x%02x", r, g, b);
    return text;
}

// A plain name for a colour, so a model that cannot see the picture still knows "dark blue" from "light grey".
[[nodiscard]] std::string ColourName(unsigned r, unsigned g, unsigned b) {
    const double red = r / 255.0;
    const double green = g / 255.0;
    const double blue = b / 255.0;
    const double high = std::max({ red, green, blue });
    const double low = std::min({ red, green, blue });
    const double value = high;
    const double chroma = high - low;
    const double saturation = high <= 0.0 ? 0.0 : chroma / high;
    if (value < 0.12) return "black";
    if (saturation < 0.16) {
        if (value > 0.92) return "white";
        if (value > 0.7) return "light grey";
        if (value > 0.4) return "grey";
        return "dark grey";
    }
    double hue = 0.0;
    if (chroma > 0.0) {
        if (high == red) hue = std::fmod((green - blue) / chroma, 6.0);
        else if (high == green) hue = (blue - red) / chroma + 2.0;
        else hue = (red - green) / chroma + 4.0;
        hue *= 60.0;
        if (hue < 0.0) hue += 360.0;
    }
    std::string name;
    if (hue < 15.0 || hue >= 345.0) name = "red";
    else if (hue < 40.0) name = "orange";
    else if (hue < 70.0) name = "yellow";
    else if (hue < 165.0) name = "green";
    else if (hue < 195.0) name = "cyan";
    else if (hue < 255.0) name = "blue";
    else if (hue < 290.0) name = "purple";
    else name = "pink";
    if (name == "orange" && value < 0.5) name = "brown";
    if (value < 0.4) return "dark " + name;
    if (saturation < 0.45 && value > 0.75) return "light " + name;
    return name;
}

struct Pixels {
    unsigned width = 0U;
    unsigned height = 0U;
    std::vector<std::uint8_t> rgba;
};

#if KB_CLI_RENDERER
struct ImageDeleter {
    void operator()(bimg::ImageContainer* container) const noexcept {
        if (container != nullptr) bimg::imageFree(container);
    }
};

[[nodiscard]] bool Decode(const fs::path& file, Pixels& pixels, std::string& error) {
    std::ifstream input{ file, std::ios::binary };
    if (!input) {
        error = "cannot read " + file.filename().string();
        return false;
    }
    const std::vector<char> bytes{ std::istreambuf_iterator<char>{ input }, std::istreambuf_iterator<char>{} };
    if (bytes.empty()) {
        error = file.filename().string() + " is empty";
        return false;
    }
    bx::DefaultAllocator allocator;
    bx::Error parseError;
    const std::unique_ptr<bimg::ImageContainer, ImageDeleter> image{ bimg::imageParse(
        &allocator, bytes.data(), static_cast<std::uint32_t>(bytes.size()), bimg::TextureFormat::RGBA8, &parseError) };
    if (image == nullptr || !parseError.isOk() || image->m_width == 0U || image->m_height == 0U) {
        error = file.filename().string() + " is not a readable picture";
        return false;
    }
    pixels.width = image->m_width;
    pixels.height = image->m_height;
    const auto* data = static_cast<const std::uint8_t*>(image->m_data);
    pixels.rgba.assign(data, data + static_cast<std::size_t>(pixels.width) * pixels.height * 4U);
    return true;
}

#else
[[nodiscard]] bool Decode(const fs::path&, Pixels&, std::string& error) {
    error = "pictures cannot be read in this build";
    return false;
}
#endif

// ---- A compressed PNG, made here: the writer of the image library stores its rows uncompressed, which for a picture
// sent to a model is four times the bytes. RGB, filter "Sub", LZ77 and the fixed Huffman code of deflate.

class BitWriter {
public:
    void Bits(std::uint32_t value, unsigned count) {
        for (unsigned index = 0U; index < count; ++index) Bit((value >> index) & 1U);
    }
    // Huffman codes go most significant bit first.
    void Code(std::uint32_t code, unsigned count) {
        for (unsigned index = count; index-- > 0U;) Bit((code >> index) & 1U);
    }
    std::vector<std::uint8_t> Finish() {
        if (used_ != 0U) bytes_.push_back(current_);
        return std::move(bytes_);
    }

private:
    void Bit(std::uint32_t bit) {
        current_ = static_cast<std::uint8_t>(current_ | (bit << used_));
        if (++used_ == 8U) {
            bytes_.push_back(current_);
            current_ = 0U;
            used_ = 0U;
        }
    }
    std::vector<std::uint8_t> bytes_;
    std::uint8_t current_ = 0U;
    unsigned used_ = 0U;
};

void WriteLiteralOrLength(BitWriter& out, unsigned symbol) {
    if (symbol < 144U) out.Code(0x30U + symbol, 8U);
    else if (symbol < 256U) out.Code(0x190U + (symbol - 144U), 9U);
    else if (symbol < 280U) out.Code(symbol - 256U, 7U);
    else out.Code(0xC0U + (symbol - 280U), 8U);
}

[[nodiscard]] std::vector<std::uint8_t> Deflate(const std::vector<std::uint8_t>& raw) {
    constexpr std::array<unsigned, 29> lengthBase{ 3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258 };
    constexpr std::array<unsigned, 29> lengthExtra{ 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0 };
    constexpr std::array<unsigned, 30> distanceBase{ 1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577 };
    constexpr std::array<unsigned, 30> distanceExtra{ 0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13 };
    constexpr std::size_t window = 32768U;
    constexpr std::uint32_t hashSize = 1U << 15U;
    constexpr int chain = 24;
    BitWriter out;
    out.Bits(1U, 1U); // the last block
    out.Bits(1U, 2U); // fixed Huffman codes
    std::vector<int> head(hashSize, -1);
    std::vector<int> previous(raw.size(), -1);
    const auto hash = [&raw](std::size_t at) {
        return ((static_cast<std::uint32_t>(raw[at]) << 10U) ^ (static_cast<std::uint32_t>(raw[at + 1U]) << 5U) ^ raw[at + 2U]) & (hashSize - 1U);
    };
    std::size_t at = 0U;
    while (at < raw.size()) {
        std::size_t bestLength = 0U;
        std::size_t bestDistance = 0U;
        if (at + 3U <= raw.size()) {
            const std::uint32_t key = hash(at);
            int candidate = head[key];
            const std::size_t limit = std::min<std::size_t>(258U, raw.size() - at);
            for (int tries = 0; candidate >= 0 && tries < chain && at - static_cast<std::size_t>(candidate) <= window; ++tries) {
                std::size_t length = 0U;
                while (length < limit && raw[static_cast<std::size_t>(candidate) + length] == raw[at + length]) ++length;
                if (length > bestLength) {
                    bestLength = length;
                    bestDistance = at - static_cast<std::size_t>(candidate);
                    if (length == limit) break;
                }
                candidate = previous[static_cast<std::size_t>(candidate)];
            }
        }
        const std::size_t step = bestLength >= 3U ? bestLength : 1U;
        if (bestLength >= 3U) {
            std::size_t code = 28U;
            while (lengthBase[code] > bestLength) --code;
            WriteLiteralOrLength(out, 257U + static_cast<unsigned>(code));
            out.Bits(static_cast<std::uint32_t>(bestLength - lengthBase[code]), lengthExtra[code]);
            std::size_t distanceCode = 29U;
            while (distanceBase[distanceCode] > bestDistance) --distanceCode;
            out.Code(static_cast<std::uint32_t>(distanceCode), 5U);
            out.Bits(static_cast<std::uint32_t>(bestDistance - distanceBase[distanceCode]), distanceExtra[distanceCode]);
        } else {
            WriteLiteralOrLength(out, raw[at]);
        }
        for (std::size_t index = at; index < at + step; ++index) {
            if (index + 3U > raw.size()) break;
            const std::uint32_t key = hash(index);
            previous[index] = head[key];
            head[key] = static_cast<int>(index);
        }
        at += step;
    }
    WriteLiteralOrLength(out, 256U);
    return out.Finish();
}

[[nodiscard]] std::uint32_t Crc32(const std::uint8_t* data, std::size_t size, std::uint32_t crc = 0U) {
    static const std::array<std::uint32_t, 256> table = [] {
        std::array<std::uint32_t, 256> made{};
        for (std::uint32_t index = 0U; index < 256U; ++index) {
            std::uint32_t value = index;
            for (int bit = 0; bit < 8; ++bit) value = (value & 1U) != 0U ? 0xEDB88320U ^ (value >> 1U) : value >> 1U;
            made[index] = value;
        }
        return made;
    }();
    crc = ~crc;
    for (std::size_t index = 0U; index < size; ++index) crc = table[(crc ^ data[index]) & 0xFFU] ^ (crc >> 8U);
    return ~crc;
}

void PutBigEndian(std::vector<std::uint8_t>& out, std::uint32_t value) {
    for (int shift = 24; shift >= 0; shift -= 8) out.push_back(static_cast<std::uint8_t>(value >> static_cast<unsigned>(shift)));
}

void PutChunk(std::vector<std::uint8_t>& out, const char (&type)[5], const std::vector<std::uint8_t>& data) {
    PutBigEndian(out, static_cast<std::uint32_t>(data.size()));
    std::vector<std::uint8_t> body(type, type + 4);
    body.insert(body.end(), data.begin(), data.end());
    out.insert(out.end(), body.begin(), body.end());
    PutBigEndian(out, Crc32(body.data(), body.size()));
}

[[nodiscard]] std::vector<std::uint8_t> EncodePng(const Pixels& pixels) {
    if (pixels.width == 0U || pixels.height == 0U || pixels.rgba.size() < static_cast<std::size_t>(pixels.width) * pixels.height * 4U) return {};
    // Rows of RGB, each filtered with "Sub" (the difference to the pixel on its left). A model reads a scene from its shapes
    // and colours, not from the last three bits of each channel: with 32 levels the frame's dithering noise is gone and the
    // picture is a third of the size (a client may count the bytes of a picture as tokens).
    const auto level = [](std::uint8_t value) { return static_cast<std::uint8_t>((value & 0xF8U) | (value >> 5U)); };
    const std::size_t stride = static_cast<std::size_t>(pixels.width) * 3U;
    std::vector<std::uint8_t> raw;
    raw.reserve((stride + 1U) * pixels.height);
    for (unsigned y = 0U; y < pixels.height; ++y) {
        raw.push_back(1U);
        const std::uint8_t* row = &pixels.rgba[static_cast<std::size_t>(y) * pixels.width * 4U];
        for (unsigned x = 0U; x < pixels.width; ++x) {
            for (unsigned channel = 0U; channel < 3U; ++channel) {
                const std::uint8_t left = x == 0U ? 0U : level(row[(x - 1U) * 4U + channel]);
                raw.push_back(static_cast<std::uint8_t>(level(row[x * 4U + channel]) - left));
            }
        }
    }
    std::uint32_t a = 1U;
    std::uint32_t b = 0U;
    for (const std::uint8_t byte : raw) {
        a = (a + byte) % 65521U;
        b = (b + a) % 65521U;
    }
    std::vector<std::uint8_t> zlib{ 0x78U, 0x01U };
    const std::vector<std::uint8_t> deflated = Deflate(raw);
    zlib.insert(zlib.end(), deflated.begin(), deflated.end());
    PutBigEndian(zlib, (b << 16U) | a);
    std::vector<std::uint8_t> header;
    PutBigEndian(header, pixels.width);
    PutBigEndian(header, pixels.height);
    header.insert(header.end(), { 8U, 2U, 0U, 0U, 0U });
    std::vector<std::uint8_t> png{ 0x89U, 'P', 'N', 'G', '\r', '\n', 0x1AU, '\n' };
    PutChunk(png, "IHDR", header);
    PutChunk(png, "IDAT", zlib);
    PutChunk(png, "IEND", {});
    return png;
}

// Averages every block of source pixels into one: a clean downscale for a picture shown to a model.
[[nodiscard]] Pixels ScaleDown(const Pixels& source, unsigned targetWidth) {
    if (targetWidth >= source.width) return source;
    Pixels result;
    result.width = targetWidth;
    result.height = std::max(1U, static_cast<unsigned>(static_cast<std::uint64_t>(source.height) * targetWidth / source.width));
    result.rgba.resize(static_cast<std::size_t>(result.width) * result.height * 4U);
    for (unsigned y = 0U; y < result.height; ++y) {
        const unsigned y0 = static_cast<unsigned>(static_cast<std::uint64_t>(y) * source.height / result.height);
        const unsigned y1 = std::max(y0 + 1U, static_cast<unsigned>(static_cast<std::uint64_t>(y + 1U) * source.height / result.height));
        for (unsigned x = 0U; x < result.width; ++x) {
            const unsigned x0 = static_cast<unsigned>(static_cast<std::uint64_t>(x) * source.width / result.width);
            const unsigned x1 = std::max(x0 + 1U, static_cast<unsigned>(static_cast<std::uint64_t>(x + 1U) * source.width / result.width));
            std::array<std::uint64_t, 4> sum{};
            for (unsigned sy = y0; sy < y1 && sy < source.height; ++sy) {
                for (unsigned sx = x0; sx < x1 && sx < source.width; ++sx) {
                    const std::uint8_t* pixel = &source.rgba[(static_cast<std::size_t>(sy) * source.width + sx) * 4U];
                    for (std::size_t channel = 0U; channel < 4U; ++channel) sum[channel] += pixel[channel];
                }
            }
            const std::uint64_t count = static_cast<std::uint64_t>(y1 - y0) * (x1 - x0);
            std::uint8_t* out = &result.rgba[(static_cast<std::size_t>(y) * result.width + x) * 4U];
            for (std::size_t channel = 0U; channel < 4U; ++channel) out[channel] = static_cast<std::uint8_t>(sum[channel] / count);
        }
    }
    return result;
}

// `fullWidth` and `fullHeight` are the size of the frame; the statistics are read from the (possibly smaller) `pixels`.
[[nodiscard]] std::string Describe(const Pixels& pixels, unsigned fullWidth, unsigned fullHeight, bool& blank) {
    struct Group {
        std::uint64_t count = 0U;
        std::array<std::uint64_t, 3> sum{};
    };
    std::map<std::string, Group> groups;
    std::map<unsigned, std::uint64_t> bins;
    std::array<double, 3> mean{};
    double luminanceSum = 0.0;
    double luminanceSquares = 0.0;
    const std::uint64_t total = static_cast<std::uint64_t>(pixels.width) * pixels.height;
    for (std::uint64_t index = 0U; index < total; ++index) {
        const std::uint8_t* pixel = &pixels.rgba[index * 4U];
        for (std::size_t channel = 0U; channel < 3U; ++channel) mean[channel] += pixel[channel];
        const double luminance = (0.2126 * pixel[0] + 0.7152 * pixel[1] + 0.0722 * pixel[2]) / 255.0;
        luminanceSum += luminance;
        luminanceSquares += luminance * luminance;
        ++bins[(static_cast<unsigned>(pixel[0] >> 4U) << 8U) | (static_cast<unsigned>(pixel[1] >> 4U) << 4U) | (pixel[2] >> 4U)];
        Group& group = groups[ColourName(pixel[0], pixel[1], pixel[2])];
        ++group.count;
        for (std::size_t channel = 0U; channel < 3U; ++channel) group.sum[channel] += pixel[channel];
    }
    std::uint64_t topBin = 0U;
    for (const auto& [bin, count] : bins) topBin = std::max(topBin, count);
    const double flat = static_cast<double>(topBin) / static_cast<double>(total);
    const double luminanceMean = luminanceSum / static_cast<double>(total);
    const double luminanceSpread = std::sqrt(std::max(0.0, luminanceSquares / static_cast<double>(total) - luminanceMean * luminanceMean));
    blank = flat >= 0.995 || luminanceSpread < 0.004;

    std::string text = "capture " + std::to_string(fullWidth) + "x" + std::to_string(fullHeight);
    text += ", mean " + Hex(static_cast<unsigned>(mean[0] / static_cast<double>(total) + 0.5), static_cast<unsigned>(mean[1] / static_cast<double>(total) + 0.5),
        static_cast<unsigned>(mean[2] / static_cast<double>(total) + 0.5));
    char brightness[48]{};
    std::snprintf(brightness, sizeof(brightness), ", brightness %.0f%%", luminanceMean * 100.0);
    text += brightness;
    if (blank) text += "; the frame is blank: one flat colour, so nothing is drawn (no camera, nothing in view, or the objects are hidden)";
    else if (flat >= 0.93) text += "; nearly uniform (" + std::to_string(static_cast<int>(flat * 100.0)) + "% one colour)";
    std::vector<std::pair<std::string, Group>> ordered(groups.begin(), groups.end());
    std::ranges::sort(ordered, [](const auto& left, const auto& right) { return left.second.count > right.second.count; });
    text += "; dominant:";
    int shown = 0;
    for (const auto& [name, group] : ordered) {
        const double share = 100.0 * static_cast<double>(group.count) / static_cast<double>(total);
        if (share < 3.0 || shown == 4) break;
        text += std::string{ shown == 0 ? " " : ", " } + name + " " + Hex(static_cast<unsigned>(group.sum[0] / group.count), static_cast<unsigned>(group.sum[1] / group.count),
            static_cast<unsigned>(group.sum[2] / group.count)) + " " + std::to_string(static_cast<int>(share + 0.5)) + "%";
        ++shown;
    }
    if (shown == 0) text += " none above 3%";
    return text;
}

} // namespace

bool DescribeCapture(const fs::path& file, unsigned maxWidth, CaptureForModel& result, std::string& error) {
    Pixels full;
    if (!Decode(file, full, error)) return false;
    const Pixels shown = ScaleDown(full, std::max(16U, maxWidth));
    result.width = full.width;
    result.height = full.height;
    result.shownWidth = shown.width;
    result.shownHeight = shown.height;
    result.summary = Describe(shown, full.width, full.height, result.blank);
    result.png = shown.width == full.width ? std::vector<std::uint8_t>{} : EncodePng(shown);
    if (result.png.empty()) {
        std::ifstream input{ file, std::ios::binary };
        result.png.assign(std::istreambuf_iterator<char>{ input }, std::istreambuf_iterator<char>{});
    }
    return !result.png.empty();
}

std::string CaptureSummary(const fs::path& file) {
    Pixels full;
    std::string error;
    if (!Decode(file, full, error)) return {};
    const Pixels shown = ScaleDown(full, 640U);
    bool blank = false;
    return Describe(shown, full.width, full.height, blank);
}

std::string Base64Encode(const std::vector<std::uint8_t>& bytes) {
    constexpr std::string_view alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve((bytes.size() + 2U) / 3U * 4U);
    for (std::size_t index = 0U; index < bytes.size(); index += 3U) {
        const std::uint32_t chunk = (static_cast<std::uint32_t>(bytes[index]) << 16U) |
            (index + 1U < bytes.size() ? static_cast<std::uint32_t>(bytes[index + 1U]) << 8U : 0U) |
            (index + 2U < bytes.size() ? static_cast<std::uint32_t>(bytes[index + 2U]) : 0U);
        out += alphabet[(chunk >> 18U) & 63U];
        out += alphabet[(chunk >> 12U) & 63U];
        out += index + 1U < bytes.size() ? alphabet[(chunk >> 6U) & 63U] : '=';
        out += index + 2U < bytes.size() ? alphabet[chunk & 63U] : '=';
    }
    return out;
}

fs::path NextCapturePath(const fs::path& projectRoot, const std::set<fs::path>& taken) {
    const fs::path folder = projectRoot / "Saved" / "Agent" / "Captures";
    std::error_code error;
    for (unsigned number = 1U; number < 100000U; ++number) {
        const fs::path candidate = folder / ("capture-" + std::to_string(number) + ".png");
        if (!fs::exists(candidate, error) && !taken.contains(candidate)) return candidate;
    }
    return folder / "capture-overflow.png";
}

} // namespace kb::cli
