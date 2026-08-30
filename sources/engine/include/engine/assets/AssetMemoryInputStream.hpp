#pragma once

#include <cstdint>
#include <istream>
#include <span>
#include <streambuf>

namespace kb::assets {

class AssetMemoryStreamBuffer final : public std::streambuf {
public:
    explicit AssetMemoryStreamBuffer(std::span<const std::uint8_t> bytes) noexcept {
        begin_ = bytes.empty()
            ? &empty_
            : const_cast<char*>(reinterpret_cast<const char*>(bytes.data()));
        end_ = begin_ + bytes.size();
        setg(begin_, begin_, end_);
    }

protected:
    pos_type seekoff(off_type offset, std::ios_base::seekdir direction, std::ios_base::openmode mode) override {
        if ((mode & std::ios_base::in) == 0) {
            return pos_type{ off_type{ -1 } };
        }
        char* base = nullptr;
        if (direction == std::ios_base::beg) base = begin_;
        else if (direction == std::ios_base::cur) base = gptr();
        else if (direction == std::ios_base::end) base = end_;
        else return pos_type{ off_type{ -1 } };
        if (offset < begin_ - base || offset > end_ - base) {
            return pos_type{ off_type{ -1 } };
        }
        char* const next = base + offset;
        setg(begin_, next, end_);
        return pos_type{ next - begin_ };
    }

    pos_type seekpos(pos_type position, std::ios_base::openmode mode) override {
        return seekoff(static_cast<off_type>(position), std::ios_base::beg, mode);
    }

private:
    char empty_ = 0;
    char* begin_ = nullptr;
    char* end_ = nullptr;
};

// A non-owning, seekable input stream over loader-owned bytes. The byte vector must outlive
// the stream. No second copy is made for text parsers that already consume std::istream.
class AssetMemoryInputStream final : public std::istream {
public:
    explicit AssetMemoryInputStream(std::span<const std::uint8_t> bytes) noexcept
        : std::istream{ nullptr }, buffer_{ bytes } {
        rdbuf(&buffer_);
    }

private:
    AssetMemoryStreamBuffer buffer_;
};

} // namespace kb::assets
