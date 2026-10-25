#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

// Bounds-checked little-endian reading and writing. Every wire format in kb::net goes through
// these two classes, so a short, oversized or hostile packet can fail a read but never read past
// the end of the buffer. A failed reader stays failed: later reads return zero and Ok() is false.
namespace kb::net {

class ByteWriter final {
public:
    explicit ByteWriter(std::vector<std::uint8_t>& output) noexcept : output_(&output) {}

    void U8(std::uint8_t value) { output_->push_back(value); }
    void U16(std::uint16_t value) {
        output_->push_back(static_cast<std::uint8_t>(value & 0xFFU));
        output_->push_back(static_cast<std::uint8_t>((value >> 8U) & 0xFFU));
    }
    void U32(std::uint32_t value) {
        for (unsigned shift = 0U; shift < 32U; shift += 8U) {
            output_->push_back(static_cast<std::uint8_t>((value >> shift) & 0xFFU));
        }
    }
    void U64(std::uint64_t value) {
        for (unsigned shift = 0U; shift < 64U; shift += 8U) {
            output_->push_back(static_cast<std::uint8_t>((value >> shift) & 0xFFU));
        }
    }
    void Bytes(std::span<const std::uint8_t> bytes) { output_->insert(output_->end(), bytes.begin(), bytes.end()); }
    void Zeros(std::size_t count) { output_->insert(output_->end(), count, std::uint8_t{ 0U }); }
    [[nodiscard]] std::size_t Size() const noexcept { return output_->size(); }

private:
    std::vector<std::uint8_t>* output_;
};

class ByteReader final {
public:
    explicit ByteReader(std::span<const std::uint8_t> input) noexcept : input_(input) {}

    [[nodiscard]] std::uint8_t U8() noexcept {
        if (!Require(1U)) {
            return 0U;
        }
        return input_[offset_++];
    }
    [[nodiscard]] std::uint16_t U16() noexcept {
        if (!Require(2U)) {
            return 0U;
        }
        const std::uint16_t value = static_cast<std::uint16_t>(input_[offset_] | (input_[offset_ + 1U] << 8U));
        offset_ += 2U;
        return value;
    }
    [[nodiscard]] std::uint32_t U32() noexcept {
        if (!Require(4U)) {
            return 0U;
        }
        std::uint32_t value = 0U;
        for (unsigned index = 0U; index < 4U; ++index) {
            value |= static_cast<std::uint32_t>(input_[offset_ + index]) << (8U * index);
        }
        offset_ += 4U;
        return value;
    }
    [[nodiscard]] std::uint64_t U64() noexcept {
        if (!Require(8U)) {
            return 0U;
        }
        std::uint64_t value = 0U;
        for (unsigned index = 0U; index < 8U; ++index) {
            value |= static_cast<std::uint64_t>(input_[offset_ + index]) << (8U * index);
        }
        offset_ += 8U;
        return value;
    }
    // Returns a view of the next `count` bytes, or an empty view and a failed reader.
    [[nodiscard]] std::span<const std::uint8_t> Bytes(std::size_t count) noexcept {
        if (!Require(count)) {
            return {};
        }
        const std::span<const std::uint8_t> view = input_.subspan(offset_, count);
        offset_ += count;
        return view;
    }
    [[nodiscard]] std::size_t Remaining() const noexcept { return input_.size() - offset_; }
    [[nodiscard]] std::size_t Offset() const noexcept { return offset_; }
    [[nodiscard]] bool Ok() const noexcept { return ok_; }

private:
    [[nodiscard]] bool Require(std::size_t count) noexcept {
        if (!ok_ || count > input_.size() - offset_) {
            ok_ = false;
            return false;
        }
        return true;
    }

    std::span<const std::uint8_t> input_;
    std::size_t offset_ = 0U;
    bool ok_ = true;
};

} // namespace kb::net
