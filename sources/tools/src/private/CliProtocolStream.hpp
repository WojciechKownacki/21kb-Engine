#pragma once

#include "engine/core/JsonValue.hpp"

#include <istream>
#include <ostream>
#include <string>

namespace kb::cli {

enum class FramedRead {
    Message,    // `body` holds one message
    Malformed,  // the frame had no usable Content-Length or was too long; the stream is at the next frame
    EndOfInput, // no further complete frame
};

// On Windows a text-mode stdin and stdout change line ends, which breaks the byte count of a frame: when `in` is the
// process's own stdin both are switched to bytes. Other streams (tests) are left alone.
void UseBinaryStdio(const std::istream& in);

// One message of the base protocol that the Language Server and Debug Adapter protocols share:
// "Content-Length: <n>", an empty line, then n bytes.
[[nodiscard]] FramedRead ReadFramedMessage(std::istream& in, std::string& body);

// Writes `message` as one frame and flushes, so the editor sees it at once.
void WriteFramedMessage(std::ostream& out, const kb::core::JsonValue& message);

} // namespace kb::cli
