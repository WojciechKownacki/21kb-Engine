#pragma once

#include <cstdint>
#include <filesystem>
#include <set>
#include <string>
#include <vector>

namespace kb::cli {

// A captured frame ready for a model: a small PNG to show, and a few lines of text for a model that cannot see images.
struct CaptureForModel {
    // The picture as PNG bytes, at most `maxWidth` pixels wide (empty when it could not be made).
    std::vector<std::uint8_t> png;
    unsigned width = 0U;
    unsigned height = 0U;
    unsigned shownWidth = 0U;
    unsigned shownHeight = 0U;
    // One short paragraph: size, mean colour, whether the frame is blank or nearly uniform, the dominant colours.
    std::string summary;
    bool blank = false;
};

// Reads the PNG at `file`, scales it down to `maxWidth` and describes it. Returns false with `error` when the file cannot be read.
// Without the renderer's image library (KB_CLI_RENDERER) nothing can be decoded: it returns false.
[[nodiscard]] bool DescribeCapture(const std::filesystem::path& file, unsigned maxWidth, CaptureForModel& result, std::string& error);

// The text summary alone, for the command line (empty when the file cannot be read).
[[nodiscard]] std::string CaptureSummary(const std::filesystem::path& file);

[[nodiscard]] std::string Base64Encode(const std::vector<std::uint8_t>& bytes);

// Where the next capture goes when it names no file: Saved/Agent/Captures/capture-<n>.png inside the project, the first
// number that is neither a file nor in `taken` (captures of one transaction that do not exist yet).
[[nodiscard]] std::filesystem::path NextCapturePath(const std::filesystem::path& projectRoot, const std::set<std::filesystem::path>& taken = {});

} // namespace kb::cli
