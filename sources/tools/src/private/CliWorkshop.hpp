#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// The art and sound workshop behind `kb_cli image new` and `kb_cli sound new`: small procedural drawing and
// synthesis, so a project gets icons, textures, sprite sheets and sound effects without any outside tool.
namespace kb::cli::workshop {

struct Rgb {
    float r = 0.0F;
    float g = 0.0F;
    float b = 0.0F;
};

// "#RRGGBB" or "RRGGBB" (sRGB, as a colour picker shows it).
[[nodiscard]] bool ParseHexColor(std::string_view text, Rgb& out);

// Straight-alpha RGBA8, row 0 on top.
struct Bitmap {
    std::uint32_t width = 0U;
    std::uint32_t height = 0U;
    std::vector<std::uint8_t> rgba;
};

struct ImageRequest {
    std::string preset;
    std::string style = "flat";
    std::optional<Rgb> color;
    std::optional<Rgb> color2;
    int size = 256;
};

inline constexpr int kMinImageSize = 16;
inline constexpr int kMaxImageSize = 1024;
inline constexpr int kMaxFlameFrameSize = 512; // a flame is 8 frames in a row

[[nodiscard]] std::string ImagePresetList();
[[nodiscard]] std::string ImageStyleList();
// The picture, or nothing with `error` saying what the request got wrong.
[[nodiscard]] std::optional<Bitmap> RenderImage(const ImageRequest& request, std::string& error);

struct SoundRequest {
    std::string preset;
    double pitchSemitones = 0.0;
    std::optional<double> lengthSeconds;
};

struct Sound {
    std::uint32_t sampleRate = 22050U;
    std::vector<float> samples; // mono, -1..1
    bool loops = false;
};

inline constexpr double kMinSoundSeconds = 0.05;
inline constexpr double kMaxSoundSeconds = 30.0;

[[nodiscard]] std::string SoundPresetList();
// The canonical preset name ("pickup" is "coin"), or empty for an unknown one.
[[nodiscard]] std::string CanonicalSoundPreset(std::string_view name);
[[nodiscard]] std::optional<Sound> SynthSound(const SoundRequest& request, std::string& error);
// A 16-bit PCM mono WAV file.
[[nodiscard]] std::vector<std::uint8_t> EncodeWav(const Sound& sound);

} // namespace kb::cli::workshop
