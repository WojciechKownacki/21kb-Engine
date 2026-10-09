#include "engine/world/WorldPackChunks.hpp"

#include "engine/world/WorldCellIndex.hpp"

#include <algorithm>
#include <map>
#include <set>
#include <tuple>

namespace kb::world {
namespace {

constexpr std::size_t kMaxLabelBytes = 64U;

[[nodiscard]] std::int64_t FloorDivide(std::int64_t value, std::int64_t divisor) noexcept {
    std::int64_t quotient = value / divisor;
    if (value % divisor != 0 && ((value < 0) != (divisor < 0))) {
        --quotient;
    }
    return quotient;
}

[[nodiscard]] std::string LabelStem(std::string_view worldName) {
    std::string stem;
    for (const char character : worldName) {
        const bool allowed = (character >= 'a' && character <= 'z') || (character >= 'A' && character <= 'Z') ||
            (character >= '0' && character <= '9') || character == '_' || character == '-' || character == '.';
        stem += allowed ? character : '_';
    }
    if (stem.empty() || stem.front() == '.' || stem.front() == '-') {
        stem.insert(stem.begin(), '_');
    }
    return stem;
}

[[nodiscard]] bool Excluded(std::string_view path, std::span<const std::string> excluded) {
    return std::ranges::any_of(excluded, [path](const std::string& prefix) { return !prefix.empty() && path.starts_with(prefix); });
}

[[nodiscard]] std::string DirectoryPrefix(const std::string& file) {
    const std::size_t slash = file.rfind('/');
    return slash == std::string::npos ? std::string{} : file.substr(0U, slash + 1U);
}

} // namespace

WorldRegionChunksResult CollectWorldRegionChunks(const std::filesystem::path& contentRoot, std::span<const std::string> excludedPrefixes) {
    WorldRegionChunksResult result;
    std::vector<std::filesystem::path> indexes;
    std::error_code code;
    for (std::filesystem::recursive_directory_iterator it{ contentRoot, code }, end; !code && it != end; it.increment(code)) {
        if (it->is_regular_file(code) && it->path().extension() == WorldCellIndex::Extension) {
            indexes.push_back(it->path());
        }
    }
    if (code) {
        result.error = "could not search " + contentRoot.generic_string() + " for built worlds: " + code.message();
        return result;
    }
    std::ranges::sort(indexes);
    std::set<std::string> labels;
    for (const std::filesystem::path& indexPath : indexes) {
        const WorldCellIndexReadResult index = WorldCellIndexIO::Read(indexPath);
        if (!index.succeeded) {
            result.error = index.error;
            return result;
        }
        const std::string virtualIndex = "/Game/" + indexPath.lexically_relative(contentRoot).generic_string();
        const std::int64_t regionCells = static_cast<std::int64_t>(index.index.regionCells);
        std::map<std::tuple<std::int64_t, std::int64_t>, std::set<std::string>> regions;
        const auto add = [&](const WorldCellCoord& coord, std::string_view relative) {
            const std::string file = ResolveWorldCellPath(virtualIndex, relative);
            if (Excluded(file, excludedPrefixes)) {
                return;
            }
            regions[{ FloorDivide(coord.x, regionCells), FloorDivide(coord.z, regionCells) }].insert(DirectoryPrefix(file));
        };
        for (const WorldCellUnit& unit : index.index.units) {
            if (!unit.persistent) add(unit.coord, unit.scene);
        }
        for (const WorldCellHlod& hlod : index.index.hlods) {
            add(hlod.coord, hlod.mesh);
        }
        const std::string stem = LabelStem(index.index.worldName);
        for (const auto& [region, prefixes] : regions) {
            const std::string suffix = ".r_" + std::to_string(std::get<0>(region)) + "_" + std::to_string(std::get<1>(region));
            std::string label = stem.substr(0U, kMaxLabelBytes - std::min(kMaxLabelBytes, suffix.size())) + suffix;
            for (std::size_t copy = 2U; labels.contains(label); ++copy) {
                const std::string unique = suffix + "-" + std::to_string(copy);
                label = stem.substr(0U, kMaxLabelBytes - std::min(kMaxLabelBytes, unique.size())) + unique;
            }
            labels.insert(label);
            result.chunks.push_back({ .label = std::move(label), .prefixes = { prefixes.begin(), prefixes.end() } });
        }
    }
    result.succeeded = true;
    return result;
}

} // namespace kb::world
