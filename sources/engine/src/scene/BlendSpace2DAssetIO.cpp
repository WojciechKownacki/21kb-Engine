#include "engine/scene/BlendSpace2DAssetIO.hpp"
#include "scene/asset/io/SceneAssetBinaryIO.hpp"

#include <fstream>
#include <iomanip>
#include <limits>
#include <locale>
#include <sstream>

namespace kb::scene {
namespace {
constexpr std::size_t maximumBytes = 2U * 1024U * 1024U;
bool ReferencesValid(const BlendSpace2DAsset& asset, std::string& error) {
    for (const auto& sample : asset.samples) {
        if (sample.clipReference.size() > 4096 || sample.clipReference.find_first_of("\r\n") != std::string::npos ||
            sample.clipReference.find('\0') != std::string::npos) {
            error = "BlendSpace2D clip reference is too long or contains a control character";
            return false;
        }
    }
    return true;
}

std::optional<BlendSpace2DAsset> Read(std::istream& input, std::string& error) {
    error.clear();
    BlendSpace2DAsset asset;
    bool header = false;
    std::size_t consumed = 0;
    for (std::string line; std::getline(input, line);) {
        consumed += line.size() + 1;
        if (consumed > maximumBytes) { error = "BlendSpace2D source exceeds 2 MiB"; return std::nullopt; }
        std::istringstream row{line};
        row.imbue(std::locale::classic());
        row >> std::ws;
        if (row.eof() || row.peek() == '#') continue;
        std::string keyword;
        row >> keyword;
        if (!header) {
            unsigned version = 0;
            if (keyword != "kbblend2d" || !(row >> version) || version != 1) {
                error = "Unsupported BlendSpace2D header"; return std::nullopt;
            }
            header = true;
        } else {
            BlendSpace2DSample sample;
            if (keyword != "sample" || asset.samples.size() >= 256 ||
                !(row >> sample.x >> sample.y >> std::quoted(sample.clipReference))) {
                error = "Invalid BlendSpace2D sample row"; return std::nullopt;
            }
            asset.samples.push_back(std::move(sample));
        }
        row >> std::ws;
        if (!row.eof()) { error = "Unexpected BlendSpace2D row data"; return std::nullopt; }
    }
    if (!header || input.bad()) { error = "BlendSpace2D source could not be read"; return std::nullopt; }
    if (!ReferencesValid(asset,error) || !BlendSpace2DPlan::Build(asset,error)) return std::nullopt;
    return asset;
}
} // namespace

std::optional<BlendSpace2DAsset> BlendSpace2DAssetIO::Load(const std::filesystem::path& path, std::string& error) {
    std::error_code fileError;
    const auto bytes = std::filesystem::file_size(path,fileError);
    if (fileError || bytes > maximumBytes) { error = "BlendSpace2D file is unavailable or exceeds 2 MiB"; return std::nullopt; }
    std::ifstream input{path,std::ios::binary};
    if (!input) { error = "BlendSpace2D file could not be opened"; return std::nullopt; }
    return Read(input,error);
}

std::optional<BlendSpace2DAsset> BlendSpace2DAssetIO::Load(std::span<const std::uint8_t> bytes, std::string& error) {
    if (bytes.size() > maximumBytes) { error = "BlendSpace2D source exceeds 2 MiB"; return std::nullopt; }
    std::string text(bytes.begin(),bytes.end());
    std::istringstream input{text};
    return Read(input,error);
}

bool BlendSpace2DAssetIO::Save(const std::filesystem::path& path, const BlendSpace2DAsset& asset, std::string& error) {
    error.clear();
    if (!ReferencesValid(asset,error) || !BlendSpace2DPlan::Build(asset,error)) return false;
    std::ostringstream output;
    output.imbue(std::locale::classic());
    output << std::setprecision(std::numeric_limits<float>::max_digits10) << "kbblend2d 1\n";
    for (const auto& sample : asset.samples)
        output << "sample " << sample.x << ' ' << sample.y << ' ' << std::quoted(sample.clipReference) << '\n';
    const std::string text = output.str();
    if (!output.good() || !SceneAssetBinaryIO::WriteBytesAtomically(path,
        {reinterpret_cast<const std::uint8_t*>(text.data()),text.size()})) {
        error = "BlendSpace2D file could not be published atomically"; return false;
    }
    return true;
}
} // namespace kb::scene
