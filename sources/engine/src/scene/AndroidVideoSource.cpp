#include "scene/AndroidVideoSource.hpp"
#include <media/NdkMediaCodec.h>
#include <media/NdkMediaExtractor.h>
#include <media/NdkMediaFormat.h>
#include <linux/memfd.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstring>
#include <memory>
#include <string_view>
namespace kb::scene {
namespace {
constexpr std::int64_t kWaitMicros = 10000;
constexpr unsigned kIdleLimit = 500; // about five seconds of a codec that neither takes nor gives data
constexpr std::size_t kAudioBufferBudget = 10000;
constexpr std::uint32_t kAudioRate = 48000;
constexpr std::int32_t kColorPlanar = 19, kColorPackedPlanar = 20, kColorSemiPlanar = 21, kColorPackedSemiPlanar = 39;
constexpr std::int32_t kColorFlexible = 0x7F420888;
constexpr std::int32_t kStandardBt709 = 1, kStandardBt601Pal = 2, kStandardBt2020 = 6, kStandardBt601Ntsc = 4;
constexpr std::int32_t kTransferSt2084 = 6, kTransferHlg = 7;
constexpr std::int32_t kPcmFloat = 4;
constexpr std::int32_t kRangeFull = 1;
// Documented MediaFormat key names that the NDK only declares from API 28 on.
constexpr const char* kKeySliceHeight = "slice-height";
constexpr const char* kKeyRotation = "rotation-degrees";
constexpr const char* kKeyColorStandard = "color-standard";
constexpr const char* kKeyColorRange = "color-range";
constexpr const char* kKeyColorTransfer = "color-transfer";
constexpr const char* kKeyPcmEncoding = "pcm-encoding";
constexpr const char* kKeyCropLeft = "crop-left";
constexpr const char* kKeyCropTop = "crop-top";
constexpr const char* kKeyCropRight = "crop-right";
constexpr const char* kKeyCropBottom = "crop-bottom";

struct FormatDeleter {
    void operator()(AMediaFormat* format) const noexcept { AMediaFormat_delete(format); }
};
using FormatPtr = std::unique_ptr<AMediaFormat, FormatDeleter>;

std::int32_t IntOf(AMediaFormat* format, const char* key, std::int32_t fallback) {
    std::int32_t value = 0;
    return format != nullptr && AMediaFormat_getInt32(format, key, &value) ? value : fallback;
}

// The container bytes live in an anonymous memory file: the extractor needs a descriptor, the minimum API level
// predates the in-memory data source, and no storage directory has to be known or cleaned up.
int CreateMemoryFile(std::span<const std::byte> bytes, std::string& error) {
    const int fd = static_cast<int>(syscall(__NR_memfd_create, "kb-ui-video", MFD_CLOEXEC));
    if (fd < 0) {
        error = "UI video could not create its in-memory source file";
        return -1;
    }
    std::size_t written = 0;
    while (written < bytes.size()) {
        const ssize_t count = write(fd, bytes.data() + written, bytes.size() - written);
        if (count < 0 && errno == EINTR) continue;
        if (count <= 0) {
            close(fd);
            error = "UI video could not fill its in-memory source file";
            return -1;
        }
        written += static_cast<std::size_t>(count);
    }
    return fd;
}
}

bool NdkCodecStream::Start(AMediaExtractor* extractor, AMediaFormat* track, std::span<const std::int32_t> colorFormats, std::string& error) {
    extractor_ = extractor;
    const char* mime = nullptr;
    if (!AMediaFormat_getString(track, AMEDIAFORMAT_KEY_MIME, &mime) || mime == nullptr) {
        error = "UI video track has no media type";
        return false;
    }
    const std::size_t attempts = std::max<std::size_t>(colorFormats.size(), 1U);
    for (std::size_t attempt = 0; attempt < attempts; ++attempt) {
        codec_ = AMediaCodec_createDecoderByType(mime);
        if (codec_ == nullptr) {
            error = "no Android decoder is available for " + std::string(mime);
            return false;
        }
        if (!colorFormats.empty()) {
            requestedColorFormat_ = colorFormats[attempt];
            AMediaFormat_setInt32(track, AMEDIAFORMAT_KEY_COLOR_FORMAT, requestedColorFormat_);
        }
        if (AMediaCodec_configure(codec_, track, nullptr, nullptr, 0) == AMEDIA_OK && AMediaCodec_start(codec_) == AMEDIA_OK) return true;
        AMediaCodec_delete(codec_);
        codec_ = nullptr;
    }
    error = "the Android decoder rejected the " + std::string(mime) + " stream";
    return false;
}
void NdkCodecStream::Stop() {
    if (codec_ != nullptr) {
        AMediaCodec_stop(codec_);
        AMediaCodec_delete(codec_);
        codec_ = nullptr;
    }
    if (format_ != nullptr) {
        AMediaFormat_delete(format_);
        format_ = nullptr;
    }
}
void NdkCodecStream::Flush() {
    AMediaCodec_flush(codec_);
    inputEnded_ = false;
    endSeen_ = false;
}
bool NdkCodecStream::Feed(std::size_t slot, std::string& error) {
    std::size_t capacity = 0;
    std::uint8_t* buffer = AMediaCodec_getInputBuffer(codec_, slot, &capacity);
    if (buffer == nullptr) {
        error = "the Android video decoder gave no input buffer";
        return false;
    }
    const ssize_t size = AMediaExtractor_readSampleData(extractor_, buffer, capacity);
    media_status_t status;
    if (size < 0) {
        inputEnded_ = true;
        status = AMediaCodec_queueInputBuffer(codec_, slot, 0, 0, 0, AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM);
    } else {
        const std::int64_t time = AMediaExtractor_getSampleTime(extractor_);
        status = AMediaCodec_queueInputBuffer(codec_, slot, 0, static_cast<std::size_t>(size), static_cast<std::uint64_t>(std::max<std::int64_t>(time, 0)), 0);
        AMediaExtractor_advance(extractor_);
    }
    if (status != AMEDIA_OK) {
        error = "the Android video decoder rejected an input buffer";
        return false;
    }
    return true;
}
NdkCodecStream::Event NdkCodecStream::Next(NdkOutput& output, std::string& error) {
    if (endSeen_) return Event::End;
    for (unsigned idle = 0; idle < kIdleLimit;) {
        bool fed = false;
        if (!inputEnded_) {
            const ssize_t slot = AMediaCodec_dequeueInputBuffer(codec_, 0);
            if (slot >= 0) {
                if (!Feed(static_cast<std::size_t>(slot), error)) return Event::Failed;
                fed = true;
            }
        }
        AMediaCodecBufferInfo info{};
        const ssize_t index = AMediaCodec_dequeueOutputBuffer(codec_, &info, fed ? 0 : kWaitMicros);
        if (index >= 0) {
            if (info.flags & AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM) endSeen_ = true;
            if ((info.flags & AMEDIACODEC_BUFFER_FLAG_CODEC_CONFIG) != 0 || info.size <= 0) {
                AMediaCodec_releaseOutputBuffer(codec_, static_cast<std::size_t>(index), false);
                if (endSeen_) return Event::End;
                continue;
            }
            std::size_t capacity = 0;
            std::uint8_t* data = AMediaCodec_getOutputBuffer(codec_, static_cast<std::size_t>(index), &capacity);
            if (data == nullptr || info.offset < 0 || static_cast<std::size_t>(info.offset) + static_cast<std::size_t>(info.size) > capacity) {
                AMediaCodec_releaseOutputBuffer(codec_, static_cast<std::size_t>(index), false);
                error = "the Android video decoder returned an invalid buffer";
                return Event::Failed;
            }
            output = {static_cast<std::size_t>(index), info.presentationTimeUs, data + info.offset, static_cast<std::size_t>(info.size)};
            return Event::Buffer;
        }
        if (index == AMEDIACODEC_INFO_OUTPUT_FORMAT_CHANGED) {
            if (format_ != nullptr) AMediaFormat_delete(format_);
            format_ = AMediaCodec_getOutputFormat(codec_);
            return Event::FormatChanged;
        }
        if (index == AMEDIACODEC_INFO_OUTPUT_BUFFERS_CHANGED) continue;
        if (index == AMEDIACODEC_INFO_TRY_AGAIN_LATER) {
            if (!fed) ++idle;
            continue;
        }
        error = "the Android video decoder reported an error";
        return Event::Failed;
    }
    error = "the Android video decoder produced no output";
    return Event::Failed;
}
void NdkCodecStream::Release(const NdkOutput& output) {
    AMediaCodec_releaseOutputBuffer(codec_, output.index, false);
}

AndroidVideoSource::~AndroidVideoSource() {
    video_.Stop();
    if (extractor_ != nullptr) AMediaExtractor_delete(extractor_);
    if (fd_ >= 0) close(fd_);
}

bool AndroidVideoSource::Open(std::span<const std::byte> bytes, std::string& error) {
    fd_ = CreateMemoryFile(bytes, error);
    if (fd_ < 0) return false;
    extractor_ = AMediaExtractor_new();
    if (extractor_ == nullptr || AMediaExtractor_setDataSourceFd(extractor_, fd_, 0, static_cast<off64_t>(bytes.size())) != AMEDIA_OK) {
        error = "UI video source could not be opened by the Android media extractor";
        return false;
    }
    constexpr std::size_t kNone = static_cast<std::size_t>(-1);
    std::size_t videoTrack = kNone, audioTrack = kNone;
    FormatPtr videoFormat, audioFormat;
    std::int64_t durationUs = 0;
    const std::size_t tracks = AMediaExtractor_getTrackCount(extractor_);
    for (std::size_t i = 0; i < tracks; ++i) {
        FormatPtr format{AMediaExtractor_getTrackFormat(extractor_, i)};
        const char* mime = nullptr;
        if (!format || !AMediaFormat_getString(format.get(), AMEDIAFORMAT_KEY_MIME, &mime) || mime == nullptr) continue;
        const std::string_view type{mime};
        std::int64_t trackUs = 0;
        const bool isVideo = type.starts_with("video/") && videoTrack == kNone, isAudio = type.starts_with("audio/") && audioTrack == kNone;
        if (!isVideo && !isAudio) continue;
        if (AMediaFormat_getInt64(format.get(), AMEDIAFORMAT_KEY_DURATION, &trackUs)) durationUs = std::max(durationUs, trackUs);
        if (isVideo) {
            videoTrack = i;
            videoFormat = std::move(format);
        } else {
            audioTrack = i;
            audioFormat = std::move(format);
        }
    }
    if (videoTrack == kNone) {
        error = "UI video source has no video track";
        return false;
    }
    duration = static_cast<double>(durationUs) / 1000000.0;
    if (duration <= 0 || duration > 60) {
        error = "UI video duration must be positive and at most 60 seconds";
        return false;
    }
    const std::int32_t transfer = IntOf(videoFormat.get(), kKeyColorTransfer, 0);
    trackStandard_ = IntOf(videoFormat.get(), kKeyColorStandard, 0);
    trackRange_ = IntOf(videoFormat.get(), kKeyColorRange, 0);
    if (IntOf(videoFormat.get(), kKeyRotation, 0) != 0 || transfer == kTransferSt2084 || transfer == kTransferHlg || trackStandard_ == kStandardBt2020) {
        error = "UI video supports unrotated SDR content; rotate or tone-map the source before importing";
        return false;
    }
    const std::int32_t trackWidth = IntOf(videoFormat.get(), AMEDIAFORMAT_KEY_WIDTH, 0), trackHeight = IntOf(videoFormat.get(), AMEDIAFORMAT_KEY_HEIGHT, 0);
    if (trackWidth <= 0 || trackHeight <= 0 || trackWidth > 1920 || trackHeight > 1080) {
        error = "UI video exceeds the 1920x1080 frame budget";
        return false;
    }
    width = static_cast<std::uint32_t>(trackWidth);
    height = static_cast<std::uint32_t>(trackHeight);
    if (audioTrack == kNone) {
        error = "UI video requires a decodable audio track";
        return false;
    }
    if (!DecodeAudio(audioTrack, audioFormat.get(), error)) return false;
    AMediaExtractor_selectTrack(extractor_, videoTrack);
    AMediaExtractor_seekTo(extractor_, 0, AMEDIAEXTRACTOR_SEEK_PREVIOUS_SYNC);
    static constexpr std::int32_t kRequestedColorFormats[] = {kColorSemiPlanar, kColorPlanar};
    return video_.Start(extractor_, videoFormat.get(), kRequestedColorFormats, error);
}

bool AndroidVideoSource::DecodeAudio(std::size_t track, AMediaFormat* format, std::string& error) {
    NdkCodecStream stream;
    AMediaExtractor_selectTrack(extractor_, track);
    if (!stream.Start(extractor_, format, {}, error)) {
        error = "UI video requires a decodable audio track: " + error;
        return false;
    }
    std::int32_t rate = IntOf(format, AMEDIAFORMAT_KEY_SAMPLE_RATE, 0), channels = IntOf(format, AMEDIAFORMAT_KEY_CHANNEL_COUNT, 0);
    PcmEncoding encoding = PcmEncoding::Int16;
    std::vector<float> timeline, chunk;
    std::size_t timelineFrames = 0;
    bool started = false, placed = false, ended = false;
    for (std::size_t budget = 0; budget < kAudioBufferBudget && !ended; ++budget) {
        NdkOutput output;
        switch (stream.Next(output, error)) {
        case NdkCodecStream::Event::Failed:
            return false;
        case NdkCodecStream::Event::End:
            ended = true;
            break;
        case NdkCodecStream::Event::FormatChanged: {
            const std::int32_t newRate = IntOf(stream.Format(), AMEDIAFORMAT_KEY_SAMPLE_RATE, rate), newChannels = IntOf(stream.Format(), AMEDIAFORMAT_KEY_CHANNEL_COUNT, channels);
            const std::int32_t pcm = IntOf(stream.Format(), kKeyPcmEncoding, 2);
            if (pcm != 2 && pcm != kPcmFloat) {
                error = "UI video audio uses an unsupported PCM encoding";
                return false;
            }
            const PcmEncoding newEncoding = pcm == kPcmFloat ? PcmEncoding::Float32 : PcmEncoding::Int16;
            if (started && (newRate != rate || newChannels != channels || newEncoding != encoding)) {
                error = "audio format changes during video are unsupported";
                return false;
            }
            rate = newRate;
            channels = newChannels;
            encoding = newEncoding;
            break;
        }
        case NdkCodecStream::Event::Buffer: {
            if (rate <= 0 || channels <= 0) {
                stream.Release(output);
                error = "UI video audio has no sample rate or channel count";
                return false;
            }
            started = true;
            if (timeline.empty()) {
                timelineFrames = static_cast<std::size_t>(std::ceil(duration * rate));
                timeline.assign(timelineFrames * 2U, 0.0F);
            }
            chunk.clear();
            const bool converted = AppendPcmAsStereo(encoding, static_cast<std::uint32_t>(channels), std::span{output.data, output.size}, chunk);
            const std::int64_t first = std::llround(static_cast<double>(output.timeUs) * rate / 1000000.0);
            stream.Release(output);
            if (!converted) {
                error = "UI video audio must be mono, stereo or 5.1";
                return false;
            }
            const std::size_t skip = first < 0 ? static_cast<std::size_t>(-first) : 0U, dest = first < 0 ? 0U : static_cast<std::size_t>(first);
            const std::size_t sourceFrames = chunk.size() / 2U;
            if (skip < sourceFrames && dest < timelineFrames) {
                const std::size_t count = std::min(sourceFrames - skip, timelineFrames - dest);
                std::memcpy(timeline.data() + dest * 2U, chunk.data() + skip * 2U, count * 2U * sizeof(float));
                placed = true;
            }
            break;
        }
        }
    }
    if (!placed || !ended) {
        error = "video audio is empty or exceeds the decode budget";
        return false;
    }
    audio = ResampleStereoLinear(timeline, static_cast<std::uint32_t>(rate), kAudioRate, static_cast<std::size_t>(std::ceil(duration * kAudioRate)));
    AMediaExtractor_unselectTrack(extractor_, track);
    return true;
}

bool AndroidVideoSource::Seek(double seconds, std::string& error) {
    if (AMediaExtractor_seekTo(extractor_, static_cast<std::int64_t>(seconds * 1000000.0), AMEDIAEXTRACTOR_SEEK_PREVIOUS_SYNC) != AMEDIA_OK) {
        error = "the Android media extractor could not seek";
        return false;
    }
    video_.Flush();
    return true;
}

bool AndroidVideoSource::ReadPictureFormat(std::string& error) {
    AMediaFormat* format = video_.Format();
    YuvPicture picture;
    switch (IntOf(format, AMEDIAFORMAT_KEY_COLOR_FORMAT, kColorFlexible)) {
    case kColorPlanar:
    case kColorPackedPlanar:
        picture.layout = YuvPlaneLayout::I420;
        break;
    case kColorSemiPlanar:
    case kColorPackedSemiPlanar:
        picture.layout = YuvPlaneLayout::Nv12;
        break;
    case kColorFlexible:
        // The decoder was asked for one of the two raw layouts and reports the generic format name.
        picture.layout = video_.RequestedColorFormat() == kColorPlanar ? YuvPlaneLayout::I420 : YuvPlaneLayout::Nv12;
        break;
    default:
        error = "UI video decoder produced an unsupported pixel format";
        return false;
    }
    const std::int32_t codedWidth = IntOf(format, AMEDIAFORMAT_KEY_WIDTH, 0), codedHeight = IntOf(format, AMEDIAFORMAT_KEY_HEIGHT, 0);
    const std::int32_t stride = IntOf(format, AMEDIAFORMAT_KEY_STRIDE, codedWidth), sliceHeight = IntOf(format, kKeySliceHeight, codedHeight);
    const std::int32_t left = IntOf(format, kKeyCropLeft, 0), top = IntOf(format, kKeyCropTop, 0);
    const std::int32_t right = IntOf(format, kKeyCropRight, codedWidth - 1), bottom = IntOf(format, kKeyCropBottom, codedHeight - 1);
    if (codedWidth <= 0 || codedHeight <= 0 || stride < codedWidth || sliceHeight < codedHeight || left < 0 || top < 0 || right < left || bottom < top || right >= codedWidth || bottom >= codedHeight) {
        error = "UI video decoder reported an invalid frame layout";
        return false;
    }
    picture.width = static_cast<std::uint32_t>(right - left + 1);
    picture.height = static_cast<std::uint32_t>(bottom - top + 1);
    picture.cropLeft = static_cast<std::uint32_t>(left);
    picture.cropTop = static_cast<std::uint32_t>(top);
    picture.stride = static_cast<std::uint32_t>(stride);
    picture.sliceHeight = static_cast<std::uint32_t>(sliceHeight);
    const std::int32_t standard = IntOf(format, kKeyColorStandard, trackStandard_);
    picture.matrix = standard == kStandardBt709 ? YuvMatrix::Bt709
        : (standard == kStandardBt601Pal || standard == kStandardBt601Ntsc) ? YuvMatrix::Bt601
        : (picture.width >= 1280 || picture.height >= 720) ? YuvMatrix::Bt709 : YuvMatrix::Bt601;
    picture.fullRange = IntOf(format, kKeyColorRange, trackRange_) == kRangeFull;
    pictureFormat_ = picture;
    hasPictureFormat_ = true;
    return true;
}

bool AndroidVideoSource::NextPicture(DecodedPicture& picture, bool& end, std::string& error) {
    end = false;
    for (;;) {
        NdkOutput output;
        switch (video_.Next(output, error)) {
        case NdkCodecStream::Event::Failed:
            return false;
        case NdkCodecStream::Event::End:
            end = true;
            return true;
        case NdkCodecStream::Event::FormatChanged:
            if (!ReadPictureFormat(error)) return false;
            break;
        case NdkCodecStream::Event::Buffer:
            if (!hasPictureFormat_ && !ReadPictureFormat(error)) {
                video_.Release(output);
                return false;
            }
            picture.time = static_cast<double>(output.timeUs) / 1000000.0;
            picture.layout = pictureFormat_;
            picture.bytes.assign(output.data, output.data + output.size);
            video_.Release(output);
            return true;
        }
    }
}
}
