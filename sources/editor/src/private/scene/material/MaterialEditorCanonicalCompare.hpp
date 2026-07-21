#pragma once

#include <cstddef>
#include <ios>
#include <ostream>
#include <streambuf>
#include <string_view>

namespace kb::editor {

// "Is this document byte-identical to that canonical text?" answered without producing the text.
//
// The Material Editor derives its dirty flag by comparing the canonical (as-it-would-be-written) form of the
// working copy against the clean snapshot. Doing that by building two strings and comparing them costs two
// full serializations plus two allocations of the whole document on every single edit. Writing straight into
// this sink costs one serialization and no allocation - and it stops the moment the bytes diverge: a short
// write puts the stream into a failed state, after which the writer's remaining insertions are no-ops.
//
// Equal() is deliberately strict about length as well as content, so a document whose canonical form is a
// prefix of the reference does not read as equal.
class CanonicalTextComparer final : public std::streambuf {
public:
    explicit CanonicalTextComparer(std::string_view reference) noexcept
        : reference_(reference) {}

    [[nodiscard]] bool Equal() const noexcept { return matches_ && consumed_ == reference_.size(); }

protected:
    std::streamsize xsputn(const char* data, std::streamsize count) override {
        if (!matches_ || count <= 0) {
            return matches_ ? count : 0;
        }
        const std::size_t size = static_cast<std::size_t>(count);
        if (size > reference_.size() - consumed_ || reference_.compare(consumed_, size, data, size) != 0) {
            matches_ = false;
            return 0; // short write -> the ostream fails and stops feeding us
        }
        consumed_ += size;
        return count;
    }

    int overflow(int ch) override {
        if (ch == traits_type::eof()) {
            return traits_type::not_eof(ch);
        }
        const char value = traits_type::to_char_type(ch);
        return xsputn(&value, 1) == 1 ? ch : traits_type::eof();
    }

private:
    std::string_view reference_;
    std::size_t consumed_ = 0U;
    bool matches_ = true;
};

// Runs `writer` against a stream that compares instead of accumulating. `writer` is any callable taking a
// std::ostream& - in practice one of the asset writers.
template <typename Writer>
[[nodiscard]] inline bool CanonicalTextEquals(std::string_view reference, Writer&& writer) {
    CanonicalTextComparer comparer{ reference };
    std::ostream output{ &comparer };
    writer(output);
    return comparer.Equal();
}

} // namespace kb::editor
