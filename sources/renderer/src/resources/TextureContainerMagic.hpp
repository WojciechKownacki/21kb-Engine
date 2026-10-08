#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace kb::render {

// The texture container formats read from files: DDS, KTX and PVR3. bimg's header
// parse also accepts its own in-memory texture chunk, which carries a raw pointer
// and is only ever meant for bgfx's own buffers; bytes from a file or a package must
// be one of these formats before bimg is asked to parse them.
enum class TextureContainerKind : std::uint8_t { None, Dds, Ktx, Pvr3 };

[[nodiscard]] inline TextureContainerKind TextureContainerKindOf(const void* data, std::size_t size) noexcept {
    if (data == nullptr || size < 4U) {
        return TextureContainerKind::None;
    }
    unsigned char magic[4]{};
    std::memcpy(magic, data, sizeof(magic));
    if (magic[0] == 'D' && magic[1] == 'D' && magic[2] == 'S' && magic[3] == ' ') return TextureContainerKind::Dds;
    if (magic[0] == 0xABU && magic[1] == 'K' && magic[2] == 'T' && magic[3] == 'X') return TextureContainerKind::Ktx;
    if (magic[0] == 'P' && magic[1] == 'V' && magic[2] == 'R' && magic[3] == 3U) return TextureContainerKind::Pvr3;
    return TextureContainerKind::None;
}

} // namespace kb::render
