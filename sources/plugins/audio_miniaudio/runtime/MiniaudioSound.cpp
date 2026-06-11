#include "runtime/MiniaudioSound.hpp"

#include <string>

namespace kb::audio_miniaudio {
namespace {

[[nodiscard]] ma_uint32 SoundFlags(bool spatial) noexcept {
    ma_uint32 flags = MA_SOUND_FLAG_STREAM;
    if (!spatial) {
        flags |= MA_SOUND_FLAG_NO_SPATIALIZATION;
    }
    return flags;
}

[[nodiscard]] ma_result InitSoundFromFile(ma_engine& engine, const std::filesystem::path& path, ma_uint32 flags, ma_sound& sound) {
#if defined(_WIN32)
    const std::wstring nativePath = path.wstring();
    return ma_sound_init_from_file_w(&engine, nativePath.c_str(), flags, nullptr, nullptr, &sound);
#else
    const std::string nativePath = path.string();
    return ma_sound_init_from_file(&engine, nativePath.c_str(), flags, nullptr, nullptr, &sound);
#endif
}

} // namespace

MiniaudioSound::~MiniaudioSound() {
    Reset();
}

ma_result MiniaudioSound::InitializeFromFile(ma_engine& engine, const std::filesystem::path& path, bool spatial) {
    Reset();
    const ma_result result = InitSoundFromFile(engine, path, SoundFlags(spatial), sound_);
    initialized_ = result == MA_SUCCESS;
    return result;
}

void MiniaudioSound::Reset() noexcept {
    if (!initialized_) {
        return;
    }
    static_cast<void>(ma_sound_stop(&sound_));
    ma_sound_uninit(&sound_);
    initialized_ = false;
}

void MiniaudioSound::Apply(const MiniaudioSoundSettings& settings) noexcept {
    if (!initialized_) {
        return;
    }
    ma_sound_set_volume(&sound_, settings.volume);
    ma_sound_set_pitch(&sound_, settings.pitch);
    ma_sound_set_looping(&sound_, settings.loop ? MA_TRUE : MA_FALSE);
    ma_sound_set_spatialization_enabled(&sound_, settings.spatial ? MA_TRUE : MA_FALSE);
    ma_sound_set_position(&sound_, settings.position.x, settings.position.y, settings.position.z);
}

ma_result MiniaudioSound::Start() noexcept {
    if (!initialized_) {
        return MA_INVALID_OPERATION;
    }
    return ma_sound_start(&sound_);
}

bool MiniaudioSound::AtEnd() const noexcept {
    return initialized_ && ma_sound_at_end(&sound_) == MA_TRUE;
}

bool MiniaudioSound::IsInitialized() const noexcept {
    return initialized_;
}

} // namespace kb::audio_miniaudio
