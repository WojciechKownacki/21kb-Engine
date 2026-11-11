#pragma once
#include "scene/VideoMediaConversion.hpp"
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>
struct AMediaCodec;
struct AMediaExtractor;
struct AMediaFormat;
namespace kb::scene {
// A decoded picture before color conversion; `bytes` is the codec buffer as handed over.
struct DecodedPicture {
    double time = 0;
    YuvPicture layout;
    std::vector<std::uint8_t> bytes;
};

struct NdkOutput {
    std::size_t index = 0;
    std::int64_t timeUs = 0;
    const std::uint8_t* data = nullptr;
    std::size_t size = 0;
};

// Feeds the selected track of an extractor through a decoder and returns decoded buffers in presentation order.
class NdkCodecStream final {
public:
    enum class Event : std::uint8_t { Buffer, FormatChanged, End, Failed };
    NdkCodecStream() = default;
    NdkCodecStream(const NdkCodecStream&) = delete;
    NdkCodecStream& operator=(const NdkCodecStream&) = delete;
    ~NdkCodecStream() { Stop(); }
    // `colorFormats` lists the raw output formats to ask for, best first; empty for audio.
    [[nodiscard]] bool Start(AMediaExtractor* extractor, AMediaFormat* track, std::span<const std::int32_t> colorFormats, std::string& error);
    void Stop();
    void Flush();
    [[nodiscard]] Event Next(NdkOutput& output, std::string& error);
    void Release(const NdkOutput& output);
    [[nodiscard]] AMediaFormat* Format() const noexcept { return format_; }
    [[nodiscard]] std::int32_t RequestedColorFormat() const noexcept { return requestedColorFormat_; }
private:
    [[nodiscard]] bool Feed(std::size_t slot, std::string& error);
    AMediaCodec* codec_ = nullptr;
    AMediaExtractor* extractor_ = nullptr;
    AMediaFormat* format_ = nullptr;
    std::int32_t requestedColorFormat_ = 0;
    bool inputEnded_ = false, endSeen_ = false;
};

// NDK media backend of the UI video decoder: AMediaExtractor demuxes and AMediaCodec decodes.
class AndroidVideoSource final {
public:
    AndroidVideoSource() = default;
    AndroidVideoSource(const AndroidVideoSource&) = delete;
    AndroidVideoSource& operator=(const AndroidVideoSource&) = delete;
    ~AndroidVideoSource();
    // Opens the container, decodes the whole audio track and starts the video decoder at the first picture.
    [[nodiscard]] bool Open(std::span<const std::byte> bytes, std::string& error);
    // Continues decoding from the sync sample at or before `seconds`.
    [[nodiscard]] bool Seek(double seconds, std::string& error);
    // Next picture in presentation order; `end` is set when the stream holds no more.
    [[nodiscard]] bool NextPicture(DecodedPicture& picture, bool& end, std::string& error);

    double duration = 0;
    std::uint32_t width = 0, height = 0;
    std::vector<float> audio; // interleaved stereo, 48 kHz, ceil(duration * 48000) frames
private:
    [[nodiscard]] bool DecodeAudio(std::size_t track, AMediaFormat* format, std::string& error);
    [[nodiscard]] bool ReadPictureFormat(std::string& error);
    AMediaExtractor* extractor_ = nullptr;
    int fd_ = -1;
    NdkCodecStream video_;
    YuvPicture pictureFormat_;
    bool hasPictureFormat_ = false;
    std::int32_t trackStandard_ = 0, trackRange_ = 0;
};
}
