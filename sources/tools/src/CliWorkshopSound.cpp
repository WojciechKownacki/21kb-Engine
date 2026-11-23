#include "CliWorkshop.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

// Procedural sound effects and loops: oscillators with a pitch sweep, filtered noise and an envelope, mixed into
// a mono buffer. Everything is deterministic, so the same request always gives the same file.
namespace kb::cli::workshop {
namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr std::uint32_t kRate = 22050U;

enum class Wave { Sine, Square, Saw, Triangle };

[[nodiscard]] float Oscillator(Wave wave, double phase) {
    const double cycle = phase - std::floor(phase);
    switch (wave) {
    case Wave::Sine: return static_cast<float>(std::sin(2.0 * kPi * cycle));
    case Wave::Square: return cycle < 0.5 ? 0.7F : -0.7F;
    case Wave::Saw: return static_cast<float>(2.0 * cycle - 1.0) * 0.8F;
    case Wave::Triangle: return static_cast<float>(4.0 * std::abs(cycle - 0.5) - 1.0);
    }
    return 0.0F;
}

// Builds one sound: every time is in seconds of the preset and is stretched by `stretch`, every pitch is moved by `ratio`.
class Mixer {
public:
    Mixer(double seconds, double stretch, double ratio)
        : stretch_(stretch), ratio_(ratio), buffer_(static_cast<std::size_t>(std::lround(seconds * stretch * kRate)), 0.0F) {}

    [[nodiscard]] std::vector<float>& Buffer() { return buffer_; }

    // A tone gliding from f0 to f1 over its length: a short attack, then an exponential decay.
    void Tone(Wave wave, double f0, double f1, double start, double length, float gain, double decay, double attack = 0.004) {
        const std::size_t first = Index(start);
        const std::size_t count = std::max<std::size_t>(1U, static_cast<std::size_t>(length * stretch_ * kRate));
        double phase = 0.0;
        for (std::size_t i = 0U; i < count && first + i < buffer_.size(); ++i) {
            const double t = static_cast<double>(i) / static_cast<double>(count);
            const double frequency = f0 * ratio_ * std::pow(f1 / f0, t);
            phase += frequency / kRate;
            const double seconds = static_cast<double>(i) / kRate;
            const double rise = std::min(1.0, seconds / attack);
            const double fall = std::min(1.0, static_cast<double>(count - i) / (0.006 * kRate));
            buffer_[first + i] += Oscillator(wave, phase) * gain * static_cast<float>(rise * fall * std::exp(-decay * t));
        }
    }

    // Noise through a one-pole low-pass whose cut-off moves from `from` to `to` Hz.
    void Noise(double start, double length, float gain, double decay, double from, double to) {
        const std::size_t first = Index(start);
        const std::size_t count = std::max<std::size_t>(1U, static_cast<std::size_t>(length * stretch_ * kRate));
        float state = 0.0F;
        for (std::size_t i = 0U; i < count && first + i < buffer_.size(); ++i) {
            const double t = static_cast<double>(i) / static_cast<double>(count);
            const double cutoff = from * ratio_ * std::pow(to / from, t);
            const float coefficient = static_cast<float>(1.0 - std::exp(-2.0 * kPi * cutoff / kRate));
            state += coefficient * (Next() - state);
            const double fall = std::min(1.0, static_cast<double>(count - i) / (0.006 * kRate)) * std::min(1.0, static_cast<double>(i) / (0.002 * kRate));
            // the one-pole filter takes energy out of the noise; the gain makes up for a low cut-off
            const float makeup = 1.0F + 1.5F * (1.0F - std::min(1.0F, coefficient * 4.0F));
            buffer_[first + i] += state * gain * makeup * static_cast<float>(fall * std::exp(-decay * t));
        }
    }

    [[nodiscard]] double Ratio() const { return ratio_; }
    [[nodiscard]] double Stretch() const { return stretch_; }

private:
    [[nodiscard]] std::size_t Index(double start) const { return static_cast<std::size_t>(std::max(0.0, start * stretch_) * kRate); }

    [[nodiscard]] float Next() {
        seed_ = seed_ * 1664525U + 1013904223U;
        return static_cast<float>(seed_ >> 8U) / 8388608.0F - 1.0F;
    }

    double stretch_;
    double ratio_;
    std::uint32_t seed_ = 0x2468ACEU;
    std::vector<float> buffer_;
};

struct PresetInfo {
    std::string_view name;
    double defaultSeconds;
    bool loops;
};

constexpr PresetInfo kPresets[]{
    { "jump", 0.28, false },
    { "coin", 0.45, false },
    { "hit", 0.22, false },
    { "explosion", 1.2, false },
    { "click", 0.06, false },
    { "laser", 0.35, false },
    { "powerup", 0.55, false },
    { "footstep", 0.2, false },
    { "wind", 4.0, true },
    { "music", 8.0, true },
};

[[nodiscard]] const PresetInfo* FindPreset(std::string_view name) {
    for (const PresetInfo& preset : kPresets) {
        if (preset.name == name) return &preset;
    }
    return nullptr;
}

double MidiToHz(int note) { return 440.0 * std::pow(2.0, (note - 69) / 12.0); }

void BuildJump(Mixer& m) {
    m.Tone(Wave::Square, 260.0, 640.0, 0.0, 0.28, 0.5F, 2.5);
    m.Tone(Wave::Triangle, 130.0, 320.0, 0.0, 0.28, 0.35F, 2.5);
}

void BuildCoin(Mixer& m) {
    m.Tone(Wave::Square, 988.0, 988.0, 0.0, 0.08, 0.45F, 0.8);
    m.Tone(Wave::Square, 1319.0, 1319.0, 0.07, 0.38, 0.45F, 4.0);
    m.Tone(Wave::Sine, 2638.0, 2638.0, 0.07, 0.3, 0.12F, 5.0);
}

void BuildHit(Mixer& m) {
    m.Noise(0.0, 0.14, 0.7F, 5.0, 6000.0, 700.0);
    m.Tone(Wave::Sine, 220.0, 70.0, 0.0, 0.22, 0.8F, 4.0);
}

void BuildExplosion(Mixer& m) {
    m.Noise(0.0, 1.2, 1.0F, 3.2, 4500.0, 110.0);
    m.Noise(0.06, 0.8, 0.5F, 3.0, 2500.0, 90.0);
    m.Tone(Wave::Sine, 95.0, 32.0, 0.0, 0.9, 0.9F, 3.5);
}

void BuildClick(Mixer& m) {
    m.Tone(Wave::Triangle, 1800.0, 1200.0, 0.0, 0.05, 0.6F, 5.0, 0.001);
    m.Noise(0.0, 0.01, 0.2F, 2.0, 9000.0, 3000.0);
}

void BuildLaser(Mixer& m) {
    m.Tone(Wave::Saw, 1900.0, 180.0, 0.0, 0.32, 0.45F, 2.0, 0.002);
    m.Tone(Wave::Square, 1920.0, 182.0, 0.0, 0.32, 0.25F, 2.4, 0.002);
}

void BuildPowerUp(Mixer& m) {
    constexpr int kNotes[]{ 72, 76, 79, 84, 88, 91 };
    for (std::size_t i = 0U; i < std::size(kNotes); ++i) {
        const bool last = i + 1U == std::size(kNotes);
        const double hz = MidiToHz(kNotes[i]);
        m.Tone(Wave::Square, hz, hz, 0.065 * static_cast<double>(i), last ? 0.22 : 0.09, 0.38F, last ? 3.0 : 1.2);
    }
}

void BuildFootstep(Mixer& m) {
    m.Noise(0.0, 0.14, 0.9F, 5.0, 1000.0, 180.0);
    m.Tone(Wave::Sine, 105.0, 55.0, 0.0, 0.15, 0.6F, 5.0);
}

// Wind: noise between two low-pass filters (a band), its loudness rolling on a cycle that fits the loop. The
// last stretch is faded into the first, so the loop has no click.
std::vector<float> BuildWind(double seconds, double ratio) {
    const std::size_t length = static_cast<std::size_t>(seconds * kRate);
    const std::size_t fade = std::min<std::size_t>(length / 4U, static_cast<std::size_t>(0.4 * kRate));
    std::vector<float> raw(length + fade, 0.0F);
    std::uint32_t seed = 0x13579BU;
    float low = 0.0F;
    float low2 = 0.0F;
    float lower = 0.0F;
    const float a1 = static_cast<float>(1.0 - std::exp(-2.0 * kPi * 900.0 * ratio / kRate));
    const float a2 = static_cast<float>(1.0 - std::exp(-2.0 * kPi * 140.0 * ratio / kRate));
    for (std::size_t i = 0U; i < raw.size(); ++i) {
        seed = seed * 1664525U + 1013904223U;
        const float noise = static_cast<float>(seed >> 8U) / 8388608.0F - 1.0F;
        low += a1 * (noise - low);
        low2 += a1 * (low - low2);
        lower += a2 * (low2 - lower);
        const double t = static_cast<double>(i) / static_cast<double>(length);
        const double roll = 0.55 + 0.3 * std::sin(2.0 * kPi * 2.0 * t) + 0.15 * std::sin(2.0 * kPi * 3.0 * t + 1.0);
        raw[i] = (low2 - lower) * 4.5F * static_cast<float>(roll);
    }
    std::vector<float> loop(raw.begin(), raw.begin() + static_cast<std::ptrdiff_t>(length));
    for (std::size_t i = 0U; i < fade; ++i) {
        const float w = static_cast<float>(i) / static_cast<float>(fade);
        loop[i] = raw[i] * w + raw[length + i] * (1.0F - w);
    }
    return loop;
}

// A four-chord loop (A minor, F, C, G): a bass, an arpeggio and a soft pad. Notes that ring past the end wrap
// to the start, so the loop is seamless.
std::vector<float> BuildMusic(double seconds, double ratio) {
    const std::size_t length = static_cast<std::size_t>(seconds * kRate);
    const double beat = seconds / 16.0;
    constexpr int kChords[4][3]{ { 57, 60, 64 }, { 53, 57, 60 }, { 48, 52, 55 }, { 55, 59, 62 } };
    Mixer m(seconds + beat * 4.0, 1.0, ratio);
    for (int chord = 0; chord < 4; ++chord) {
        const double chordStart = beat * 4.0 * chord;
        const int* notes = kChords[chord];
        for (int half = 0; half < 2; ++half) {
            m.Tone(Wave::Sine, MidiToHz(notes[0] - 12), MidiToHz(notes[0] - 12), chordStart + beat * 2.0 * half, beat * 2.0, 0.5F, 2.0, 0.01);
        }
        for (int step = 0; step < 8; ++step) {
            constexpr int kPattern[4]{ 0, 1, 2, 1 };
            const double hz = MidiToHz(notes[kPattern[step % 4]] + 12);
            m.Tone(Wave::Triangle, hz, hz, chordStart + beat * 0.5 * step, beat * 0.9, 0.22F, 3.0, 0.005);
        }
        for (int voice = 0; voice < 3; ++voice) {
            m.Tone(Wave::Sine, MidiToHz(notes[voice]), MidiToHz(notes[voice]), chordStart, beat * 4.2, 0.1F, 0.5, beat * 1.2);
        }
    }
    std::vector<float> loop(length, 0.0F);
    const std::vector<float>& mixed = m.Buffer();
    for (std::size_t i = 0U; i < mixed.size(); ++i) loop[i % length] += mixed[i];
    return loop;
}

} // namespace

std::string SoundPresetList() {
    std::string list;
    for (const PresetInfo& preset : kPresets) list += (list.empty() ? "" : ", ") + std::string{ preset.name };
    return list + " (pickup is coin, power-up is powerup)";
}

std::string CanonicalSoundPreset(std::string_view name) {
    std::string lower;
    for (const char c : name) lower += (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
    if (lower == "pickup") lower = "coin";
    if (lower == "power-up" || lower == "power_up") lower = "powerup";
    if (lower == "ambient" || lower == "wind-loop") lower = "wind";
    if (lower == "music-loop") lower = "music";
    return FindPreset(lower) != nullptr ? lower : std::string{};
}

std::optional<Sound> SynthSound(const SoundRequest& request, std::string& error) {
    const std::string name = CanonicalSoundPreset(request.preset);
    if (name.empty()) {
        error = "unknown preset '" + request.preset + "'; presets: " + SoundPresetList();
        return std::nullopt;
    }
    const PresetInfo& preset = *FindPreset(name);
    if (request.pitchSemitones < -24.0 || request.pitchSemitones > 24.0) {
        error = "--pitch takes -24 to 24 semitones (12 is one octave up)";
        return std::nullopt;
    }
    const double seconds = request.lengthSeconds.value_or(preset.defaultSeconds);
    if (seconds < kMinSoundSeconds || seconds > kMaxSoundSeconds) {
        error = "--length takes " + std::to_string(kMinSoundSeconds) + " to " + std::to_string(kMaxSoundSeconds) + " seconds";
        return std::nullopt;
    }
    const double ratio = std::pow(2.0, request.pitchSemitones / 12.0);
    const double stretch = seconds / preset.defaultSeconds;
    Sound sound;
    sound.sampleRate = kRate;
    sound.loops = preset.loops;
    if (name == "wind") {
        sound.samples = BuildWind(seconds, ratio);
    } else if (name == "music") {
        sound.samples = BuildMusic(seconds, ratio);
    } else {
        Mixer mixer(preset.defaultSeconds, stretch, ratio);
        if (name == "jump") BuildJump(mixer);
        else if (name == "coin") BuildCoin(mixer);
        else if (name == "hit") BuildHit(mixer);
        else if (name == "explosion") BuildExplosion(mixer);
        else if (name == "click") BuildClick(mixer);
        else if (name == "laser") BuildLaser(mixer);
        else if (name == "powerup") BuildPowerUp(mixer);
        else BuildFootstep(mixer);
        sound.samples = std::move(mixer.Buffer());
    }
    float peak = 0.0F;
    for (const float sample : sound.samples) peak = std::max(peak, std::abs(sample));
    const float scale = peak > 0.0001F ? 0.85F / peak : 1.0F;
    for (float& sample : sound.samples) sample *= scale;
    return sound;
}

std::vector<std::uint8_t> EncodeWav(const Sound& sound) {
    const std::uint32_t dataBytes = static_cast<std::uint32_t>(sound.samples.size() * 2U);
    std::vector<std::uint8_t> bytes;
    bytes.reserve(44U + dataBytes);
    const auto put32 = [&bytes](std::uint32_t v) { for (int i = 0; i < 4; ++i) bytes.push_back(static_cast<std::uint8_t>((v >> (8 * i)) & 0xFFU)); };
    const auto put16 = [&bytes](std::uint32_t v) { for (int i = 0; i < 2; ++i) bytes.push_back(static_cast<std::uint8_t>((v >> (8 * i)) & 0xFFU)); };
    const auto tag = [&bytes](const char* text) { for (int i = 0; i < 4; ++i) bytes.push_back(static_cast<std::uint8_t>(text[i])); };
    tag("RIFF");
    put32(36U + dataBytes);
    tag("WAVE");
    tag("fmt ");
    put32(16U);
    put16(1U);
    put16(1U);
    put32(sound.sampleRate);
    put32(sound.sampleRate * 2U);
    put16(2U);
    put16(16U);
    tag("data");
    put32(dataBytes);
    for (const float sample : sound.samples) {
        const long value = std::lround(std::clamp(sample, -1.0F, 1.0F) * 32767.0F);
        put16(static_cast<std::uint32_t>(static_cast<std::uint16_t>(static_cast<std::int16_t>(value))));
    }
    return bytes;
}

} // namespace kb::cli::workshop
