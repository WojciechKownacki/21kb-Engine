#pragma once

#include <bx/bx.h>

#include <cstdio>
#include <cstdint>
#include <cstdlib>

namespace kb::render::tests {

inline bool FailRendererAssertion(const bx::Location& location, std::uint32_t, const char* format, va_list arguments) {
    std::fprintf(stderr, "renderer_assertion %s:%u: ", location.filePath, location.line);
    std::vfprintf(stderr, format, arguments);
    std::fputc('\n', stderr);
    std::fflush(stderr);
    std::abort();
}

inline void InstallRendererAssertionPolicy() {
    // bx's default handler can report an assertion and continue. Validation must fail instead.
    bx::setAssertHandler(&FailRendererAssertion);
}

inline void ProbeRendererAssertion() {
    static_cast<void>(bx::assertFunction(bx::Location{__FILE__, __LINE__}, 0U, "Renderer assertion probe"));
}

} // namespace kb::render::tests
