#include "Backend.hpp"

// Platforms without a backend. The client reports it plainly instead of failing somewhere deeper.
namespace kb::http::detail {

HopResponse PerformHop(const HopRequest&, CancelToken&) {
    HopResponse response;
    response.status = Status::Unsupported;
    response.error = "this platform has no HTTP backend yet (Windows uses WinHTTP); no TLS library is bundled";
    return response;
}

} // namespace kb::http::detail
