#pragma once

#include <charconv>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace kb::library {

// LIB-062: allocation-free text formatting for the hot path (e.g. a
// per-frame debug HUD string). TextFormatBuffer wraps a caller-provided
// std::span<char> (a stack array, a thread_local buffer, a frame arena
// slice) and never allocates memory itself — the same "kontrolowany"
// contract as EngineLibraryCollections.hpp's NonAlloc family (LIB-059),
// applied to text: every Append* returns bool (true = fit, false = would
// have overflowed and was left completely unwritten, never a partial/
// truncated write) instead of growing, truncating silently, or throwing.
//
// Numeric conversions go through std::to_chars (C++17), which the
// standard guarantees does not allocate, rather than std::to_string or
// snprintf-style formatting (both of which may allocate internally,
// exactly the "niekontrolowana alokacja" this task rules out).
class TextFormatBuffer {
public:
    explicit TextFormatBuffer(std::span<char> storage) noexcept
        : storage_(storage) {}

    [[nodiscard]] std::size_t Length() const noexcept { return length_; }
    [[nodiscard]] std::size_t Capacity() const noexcept { return storage_.size(); }
    [[nodiscard]] bool Empty() const noexcept { return length_ == 0U; }
    [[nodiscard]] bool Full() const noexcept { return length_ >= storage_.size(); }
    [[nodiscard]] std::string_view View() const noexcept { return std::string_view{ storage_.data(), length_ }; }

    void Clear() noexcept { length_ = 0U; }

    [[nodiscard]] bool Append(std::string_view text) {
        if (text.size() > storage_.size() - length_) {
            return false;
        }
        for (const char character : text) {
            storage_[length_] = character;
            ++length_;
        }
        return true;
    }

    [[nodiscard]] bool AppendChar(char value) {
        if (Full()) {
            return false;
        }
        storage_[length_] = value;
        ++length_;
        return true;
    }

    [[nodiscard]] bool AppendInt(std::int64_t value) { return AppendViaToChars(value); }
    [[nodiscard]] bool AppendUInt(std::uint64_t value) { return AppendViaToChars(value); }

    [[nodiscard]] bool AppendFloat(double value, int precision = 6) {
        const std::to_chars_result conversion = std::to_chars(
            storage_.data() + length_, storage_.data() + storage_.size(), value, std::chars_format::fixed, precision);
        if (conversion.ec != std::errc{}) {
            return false;
        }
        length_ = static_cast<std::size_t>(conversion.ptr - storage_.data());
        return true;
    }

    [[nodiscard]] bool AppendBool(bool value) { return Append(value ? std::string_view{ "true" } : std::string_view{ "false" }); }

private:
    template <typename T>
    [[nodiscard]] bool AppendViaToChars(T value) {
        const std::to_chars_result conversion = std::to_chars(storage_.data() + length_, storage_.data() + storage_.size(), value);
        if (conversion.ec != std::errc{}) {
            return false;
        }
        length_ = static_cast<std::size_t>(conversion.ptr - storage_.data());
        return true;
    }

    std::span<char> storage_;
    std::size_t length_ = 0U;
};

} // namespace kb::library
